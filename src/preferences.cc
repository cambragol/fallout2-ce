#include "options.h"

#include <algorithm>

#include "art.h"
#include "color.h"
#include "combat.h"
#include "combat_ai.h"
#include "debug.h"
#include "delay.h"
#include "draw.h"
#include "game.h"
#include "game_mouse.h"
#include "game_sound.h"
#include "graph_lib.h"
#include "input.h"
#include "kb.h"
#include "message.h"
#include "mouse.h"
#include "palette.h"
#include "scripts.h"
#include "settings.h"
#include "svga.h"
#include "text_font.h"
#include "text_object.h"
#include "window_manager.h"

namespace fallout {

#define PREFERENCES_WINDOW_WIDTH 640
#define PREFERENCES_WINDOW_HEIGHT 480

#define PRIMARY_OPTION_VALUE_COUNT 4
#define SECONDARY_OPTION_VALUE_COUNT 2

typedef enum Preference {
    PREF_GAME_DIFFICULTY,
    PREF_COMBAT_DIFFICULTY,
    PREF_VIOLENCE_LEVEL,
    PREF_TARGET_HIGHLIGHT,
    PREF_COMBAT_LOOKS,
    PREF_COMBAT_MESSAGES,
    PREF_COMBAT_TAUNTS,
    PREF_LANGUAGE_FILTER,
    PREF_RUNNING,
    PREF_SUBTITLES,
    PREF_ITEM_HIGHLIGHT,
    PREF_COMBAT_SPEED,
    PREF_TEXT_BASE_DELAY,
    PREF_MASTER_VOLUME,
    PREF_MUSIC_VOLUME,
    PREF_SFX_VOLUME,
    PREF_SPEECH_VOLUME,
    PREF_BRIGHTNESS,
    PREF_MOUSE_SENSITIVIY,
    PREF_COUNT,
    FIRST_PRIMARY_PREF = PREF_GAME_DIFFICULTY,
    LAST_PRIMARY_PREF = PREF_COMBAT_LOOKS,
    PRIMARY_PREF_COUNT = LAST_PRIMARY_PREF - FIRST_PRIMARY_PREF + 1,
    FIRST_SECONDARY_PREF = PREF_COMBAT_MESSAGES,
    LAST_SECONDARY_PREF = PREF_ITEM_HIGHLIGHT,
    SECONDARY_PREF_COUNT = LAST_SECONDARY_PREF - FIRST_SECONDARY_PREF + 1,
    FIRST_RANGE_PREF = PREF_COMBAT_SPEED,
    LAST_RANGE_PREF = PREF_MOUSE_SENSITIVIY,
    RANGE_PREF_COUNT = LAST_RANGE_PREF - FIRST_RANGE_PREF + 1,
} Preference;

typedef enum PreferencesWindowFrm {
    PREFERENCES_WINDOW_FRM_BACKGROUND,
    // Knob (for range preferences)
    PREFERENCES_WINDOW_FRM_KNOB_OFF,
    // 4-way switch (for primary preferences)
    PREFERENCES_WINDOW_FRM_PRIMARY_SWITCH,
    // 2-way switch (for secondary preferences)
    PREFERENCES_WINDOW_FRM_SECONDARY_SWITCH,
    PREFERENCES_WINDOW_FRM_CHECKBOX_ON,
    PREFERENCES_WINDOW_FRM_CHECKBOX_OFF,
    PREFERENCES_WINDOW_FRM_6,
    // Active knob (for range preferences)
    PREFERENCES_WINDOW_FRM_KNOB_ON,
    PREFERENCES_WINDOW_FRM_LITTLE_RED_BUTTON_UP,
    PREFERENCES_WINDOW_FRM_LITTLE_RED_BUTTON_DOWN,
    PREFERENCES_WINDOW_FRM_COUNT,
} PreferencesWindowFrm;

typedef struct PreferenceDescription {
    // The number of options.
    short valuesCount;

    // Direction of rotation:
    // 0 - clockwise (incrementing value),
    // 1 - counter-clockwise (decrementing value)
    short direction;
    short knobX;
    short knobY;
    // Min x coordinate of the preference control bounding box.
    short minX;
    // Max x coordinate of the preference control bounding box.
    short maxX;
    short labelIds[PRIMARY_OPTION_VALUE_COUNT];
    int btn;
    char name[32];
    double minValue;
    double maxValue;
    int* valuePtr;
} PreferenceDescription;

static void _SetSystemPrefs();
static void _SaveSettings();
static void _RestoreSettings();
static void preferencesSetDefaults(bool a1);
static void _JustUpdate_();
static void _UpdateThing(int index);
int _SavePrefs(bool save);
static int preferencesWindowInit();
static int preferencesWindowFree();
static void _DoThing(int eventCode);

static int preferences_fatal_error();

// static globals shared for stretching
static int originalWidth = PREFERENCES_WINDOW_WIDTH;
static int originalHeight = PREFERENCES_WINDOW_HEIGHT;
static int scaledWidth = PREFERENCES_WINDOW_WIDTH;
static int scaledHeight = PREFERENCES_WINDOW_HEIGHT;

// static globals shared for stretching
static float scaleX = 1.0f;
static float scaleY = 1.0f;

// buffers for handling stretching
static uint8_t* compositeBuffer = nullptr;
static uint8_t* cleanPreferencesBuffer = nullptr;

static int screenWidth;
static int screenHeight;

// 0x48FBD0
static const int _row1Ytab[PRIMARY_PREF_COUNT] = {
    48,
    125,
    203,
    286,
    363,
};

// 0x48FBDA
static const int _row2Ytab[SECONDARY_PREF_COUNT] = {
    49,
    116,
    181,
    247,
    313,
    380,
};

// 0x48FBE6
static const int _row3Ytab[RANGE_PREF_COUNT] = {
    19,
    94,
    165,
    216,
    268,
    319,
    369,
    420,
};

// x offsets for primary preferences from the knob position
// 0x48FBF6
static const short word_48FBF6[PRIMARY_OPTION_VALUE_COUNT] = {
    2,
    25,
    46,
    46,
};

// y offsets for primary preference option values from the knob position
// 0x48FBFE
static const short word_48FBFE[PRIMARY_OPTION_VALUE_COUNT] = {
    10,
    -4,
    10,
    31,
};

// x offsets for secondary prefrence option values from the knob position
// 0x48FC06
static const short word_48FC06[SECONDARY_OPTION_VALUE_COUNT] = {
    4,
    21,
};

// y offsets for secondary preferences
// 0x48FC30
static const int dword_48FC30[SECONDARY_PREF_COUNT] = {
    66, // combat messages
    133, // combat taunts
    200, // language filter
    264, // running
    331, // subtitles
    397, // item highlight
};

// y offsets for primary preferences
// 0x48FC1C
static const int dword_48FC1C[PRIMARY_PREF_COUNT] = {
    66, // game difficulty
    143, // combat difficulty
    222, // violence level
    304, // target highlight
    382, // combat looks
};

// 0x50C168
static const double dbl_50C168 = 1.17999267578125;

// 0x50C170
static const double dbl_50C170 = 0.01124954223632812;

// 0x50C178
static const double dbl_50C178 = -0.01124954223632812;

// 0x50C180
static const double dbl_50C180 = 1.17999267578125;

// 0x50C2D0
static const double dbl_50C2D0 = -1.0;

// 0x50C2D8
static const double dbl_50C2D8 = 0.2;

// 0x50C2E0
static const double dbl_50C2E0 = 2.0;

// 0x5197CC
static const int gPreferencesWindowFrmIds[PREFERENCES_WINDOW_FRM_COUNT] = {
    240, // prefscrn.frm - options screen
    241, // prfsldof.frm - options screen
    242, // prfbknbs.frm - options screen
    243, // prflknbs.frm - options screen
    244, // prfxin.frm - options screen
    245, // prfxout.frm - options screen
    246, // prefcvr.frm - options screen
    247, // prfsldon.frm - options screen
    8, // lilredup.frm - little red button up
    9, // lilreddn.frm - little red button down
};

// 0x6637E8
static MessageList gPreferencesMessageList;

// 0x663840
static MessageListItem gPreferencesMessageListItem;

// 0x6638C8
static double gPreferencesTextBaseDelay2;

// 0x6638D0
static double gPreferencesBrightness1;

// 0x6638D8
static double gPreferencesBrightness2;

// 0x6638E0
static double gPreferencesTextBaseDelay1;

// 0x6638E8
static double gPreferencesMouseSensitivity1;

// 0x6638F0
static double gPreferencesMouseSensitivity2;

// 0x6638F8
static unsigned char* gPreferencesWindowBuffer;

// 0x663904
static int gPreferencesWindow;

// 0x663924
static int gPreferencesGameDifficulty2;

// 0x663928
static int gPreferencesCombatDifficulty2;

// 0x66392C
static int gPreferencesViolenceLevel2;

// 0x663930
static int gPreferencesTargetHighlight2;

// 0x663934
static int gPreferencesCombatLooks2;

// 0x663938
static int gPreferencesCombatMessages2;

// 0x66393C
static int gPreferencesCombatTaunts2;

// 0x663940
static int gPreferencesLanguageFilter2;

// 0x663944
static int gPreferencesRunning2;

// 0x663948
static int gPreferencesSubtitles2;

// 0x66394C
static int gPreferencesItemHighlight2;

// 0x663950
static int gPreferencesCombatSpeed2;

// 0x663954
static int gPreferencesPlayerSpeedup2;

// 0x663958
static int gPreferencesMasterVolume2;

// 0x66395C
static int gPreferencesMusicVolume2;

// 0x663960
static int gPreferencesSoundEffectsVolume2;

// 0x663964
static int gPreferencesSpeechVolume2;

// 0x663970
static int gPreferencesSoundEffectsVolume1;

// 0x663974
static int gPreferencesSubtitles1;

// 0x663978
static int gPreferencesLanguageFilter1;

// 0x66397C
static int gPreferencesSpeechVolume1;

// 0x663980
static int gPreferencesMasterVolume1;

// 0x663984
static int gPreferencesPlayerSpeedup1;

// 0x663988
static int gPreferencesCombatTaunts1;

// 0x663990
static int gPreferencesMusicVolume1;

// 0x663998
static int gPreferencesRunning1;

// 0x66399C
static int gPreferencesCombatSpeed1;

// 0x6639A0
static int _plyrspdbid;

// 0x6639A4
static int gPreferencesItemHighlight1;

// 0x6639A8
static bool _changed;

// 0x6639AC
static int gPreferencesCombatMessages1;

// 0x6639B0
static int gPreferencesTargetHighlight1;

// 0x6639B4
static int gPreferencesCombatDifficulty1;

// 0x6639B8
static int gPreferencesViolenceLevel1;

// 0x6639BC
static int gPreferencesGameDifficulty1;

// 0x6639C0
static int gPreferencesCombatLooks1;

// 0x5197F8
static PreferenceDescription gPreferenceDescriptions[PREF_COUNT] = {
    { 3, 0, 76, 71, 0, 0, { 203, 204, 205, 0 }, 0, GAME_CONFIG_GAME_DIFFICULTY_KEY, 0, 0, &gPreferencesGameDifficulty1 },
    { 3, 0, 76, 149, 0, 0, { 206, 204, 208, 0 }, 0, GAME_CONFIG_COMBAT_DIFFICULTY_KEY, 0, 0, &gPreferencesCombatDifficulty1 },
    { 4, 0, 76, 226, 0, 0, { 214, 215, 204, 216 }, 0, GAME_CONFIG_VIOLENCE_LEVEL_KEY, 0, 0, &gPreferencesViolenceLevel1 },
    { 3, 0, 76, 309, 0, 0, { 202, 201, 213, 0 }, 0, GAME_CONFIG_TARGET_HIGHLIGHT_KEY, 0, 0, &gPreferencesTargetHighlight1 },
    { 2, 0, 76, 387, 0, 0, { 202, 201, 0, 0 }, 0, GAME_CONFIG_COMBAT_LOOKS_KEY, 0, 0, &gPreferencesCombatLooks1 },
    { 2, 0, 299, 74, 0, 0, { 211, 212, 0, 0 }, 0, GAME_CONFIG_COMBAT_MESSAGES_KEY, 0, 0, &gPreferencesCombatMessages1 },
    { 2, 0, 299, 141, 0, 0, { 202, 201, 0, 0 }, 0, GAME_CONFIG_COMBAT_TAUNTS_KEY, 0, 0, &gPreferencesCombatTaunts1 },
    { 2, 0, 299, 207, 0, 0, { 202, 201, 0, 0 }, 0, GAME_CONFIG_LANGUAGE_FILTER_KEY, 0, 0, &gPreferencesLanguageFilter1 },
    { 2, 0, 299, 271, 0, 0, { 209, 219, 0, 0 }, 0, GAME_CONFIG_RUNNING_KEY, 0, 0, &gPreferencesRunning1 },
    { 2, 0, 299, 338, 0, 0, { 202, 201, 0, 0 }, 0, GAME_CONFIG_SUBTITLES_KEY, 0, 0, &gPreferencesSubtitles1 },
    { 2, 0, 299, 404, 0, 0, { 202, 201, 0, 0 }, 0, GAME_CONFIG_ITEM_HIGHLIGHT_KEY, 0, 0, &gPreferencesItemHighlight1 },
    { 2, 0, 374, 50, 0, 0, { 207, 210, 0, 0 }, 0, GAME_CONFIG_COMBAT_SPEED_KEY, 0.0, 50.0, &gPreferencesCombatSpeed1 },
    { 3, 0, 374, 125, 0, 0, { 217, 209, 218, 0 }, 0, GAME_CONFIG_TEXT_BASE_DELAY_KEY, 1.0, 6.0, nullptr },
    { 4, 0, 374, 197, 0, 0, { 202, 221, 209, 222 }, 0, GAME_CONFIG_MASTER_VOLUME_KEY, 0, 32767.0, &gPreferencesMasterVolume1 },
    { 4, 0, 374, 248, 0, 0, { 202, 221, 209, 222 }, 0, GAME_CONFIG_MUSIC_VOLUME_KEY, 0, 32767.0, &gPreferencesMusicVolume1 },
    { 4, 0, 374, 299, 0, 0, { 202, 221, 209, 222 }, 0, GAME_CONFIG_SNDFX_VOLUME_KEY, 0, 32767.0, &gPreferencesSoundEffectsVolume1 },
    { 4, 0, 374, 350, 0, 0, { 202, 221, 209, 222 }, 0, GAME_CONFIG_SPEECH_VOLUME_KEY, 0, 32767.0, &gPreferencesSpeechVolume1 },
    { 2, 0, 374, 401, 0, 0, { 207, 223, 0, 0 }, 0, GAME_CONFIG_BRIGHTNESS_KEY, 1.0, 1.17999267578125, nullptr },
    { 2, 0, 374, 453, 0, 0, { 207, 218, 0, 0 }, 0, GAME_CONFIG_MOUSE_SENSITIVITY_KEY, 1.0, 2.5, nullptr },
};

static FrmImage _preferencesFrmImages[PREFERENCES_WINDOW_FRM_COUNT];
static int _oldFont;

// Globals to hold the stretched knob and its dimensions
unsigned char* gKnobImageStretched = nullptr;
unsigned char* gKnobOffImageStretched = nullptr;
int gKnobStretchedWidth = 0;
int gKnobStretchedHeight = 0;

void prepareStretchedKnob(double scaleX, double scaleY)
{
    constexpr int knobOrigW = 21;
    constexpr int knobOrigH = 12;

    int stW = static_cast<int>(knobOrigW * scaleX);
    int stH = static_cast<int>(knobOrigH * scaleY);
    if (stW < 1)
        stW = 1;
    if (stH < 1)
        stH = 1;

    // Free old buffers
    if (gKnobImageStretched)
        SDL_free(gKnobImageStretched);
    if (gKnobOffImageStretched)
        SDL_free(gKnobOffImageStretched);

    gKnobImageStretched = reinterpret_cast<unsigned char*>(SDL_malloc(stW * stH));
    gKnobOffImageStretched = reinterpret_cast<unsigned char*>(SDL_malloc(stW * stH));
    if (!gKnobImageStretched || !gKnobOffImageStretched) {
        gKnobStretchedWidth = gKnobStretchedHeight = 0;
        return;
    }

    gKnobStretchedWidth = stW;
    gKnobStretchedHeight = stH;

    // Stretch "knob ON"
    blitBufferToBufferStretchAndFixEdges(
        _preferencesFrmImages[PREFERENCES_WINDOW_FRM_KNOB_ON].getData(),
        21, 12, 21,
        gKnobImageStretched,
        gKnobStretchedWidth, gKnobStretchedHeight, gKnobStretchedWidth);

    // Stretch "knob OFF"
    blitBufferToBufferStretchAndFixEdges(
        _preferencesFrmImages[PREFERENCES_WINDOW_FRM_KNOB_OFF].getData(),
        21, 12, 21,
        gKnobOffImageStretched,
        gKnobStretchedWidth, gKnobStretchedHeight, gKnobStretchedWidth);
}

// Globals to hold the stretched buttons and their dimensions
unsigned char* gPrimarySwitchStretched = nullptr;
unsigned char* gSecondarySwitchStretched = nullptr;
int gPrimarySwitchStretchedWidth = 0;
int gPrimarySwitchStretchedHeight = 0;
int gSecondarySwitchStretchedWidth = 0;
int gSecondarySwitchStretchedHeight = 0;

void prepareStretchedButtons(double scaleX, double scaleY)
{
    // Original dimensions
    constexpr int primaryW = 46, primaryH = 47, primaryStates = 4;
    constexpr int secondaryW = 22, secondaryH = 25, secondaryStates = 2;

    // Scaled dimensions
    const int scaledPrimaryW = static_cast<int>(primaryW * scaleX);
    const int scaledPrimaryH = static_cast<int>(primaryH * scaleY);
    const int scaledSecondaryW = static_cast<int>(secondaryW * scaleX);
    const int scaledSecondaryH = static_cast<int>(secondaryH * scaleY);

    // Free old buffers
    SDL_free(gPrimarySwitchStretched);
    SDL_free(gSecondarySwitchStretched);

    // Allocate new buffers
    gPrimarySwitchStretched = static_cast<unsigned char*>(
        SDL_malloc(scaledPrimaryW * scaledPrimaryH * primaryStates));
    gSecondarySwitchStretched = static_cast<unsigned char*>(
        SDL_malloc(scaledSecondaryW * scaledSecondaryH * secondaryStates));

    // Allocation failed
    if (!gPrimarySwitchStretched || !gSecondarySwitchStretched) {
        gPrimarySwitchStretchedWidth = gPrimarySwitchStretchedHeight = 0;
        gSecondarySwitchStretchedWidth = gSecondarySwitchStretchedHeight = 0;
        return;
    }

    // Store scaled dimensions
    gPrimarySwitchStretchedWidth = scaledPrimaryW;
    gPrimarySwitchStretchedHeight = scaledPrimaryH;
    gSecondarySwitchStretchedWidth = scaledSecondaryW;
    gSecondarySwitchStretchedHeight = scaledSecondaryH;

    // Stretch and fix edge pixels
    blitBufferToBufferStretchAndFixEdges(
        _preferencesFrmImages[PREFERENCES_WINDOW_FRM_PRIMARY_SWITCH].getData(),
        primaryW, primaryH, primaryW,
        gPrimarySwitchStretched,
        scaledPrimaryW, scaledPrimaryH, scaledPrimaryW,
        primaryStates
    );

    blitBufferToBufferStretchAndFixEdges(
        _preferencesFrmImages[PREFERENCES_WINDOW_FRM_SECONDARY_SWITCH].getData(),
        secondaryW, secondaryH, secondaryW,
        gSecondarySwitchStretched,
        scaledSecondaryW, scaledSecondaryH, scaledSecondaryW,
        secondaryStates
    );
}

int preferencesInit()
{
    for (int index = 0; index < 11; index++) {
        gPreferenceDescriptions[index].direction = 0;
    }

    _SetSystemPrefs();

    return 0;
}

// 0x492AA8
static void _SetSystemPrefs()
{
    preferencesSetDefaults(false);

    gPreferencesGameDifficulty1 = settings.preferences.game_difficulty;
    gPreferencesCombatDifficulty1 = settings.preferences.combat_difficulty;
    gPreferencesViolenceLevel1 = settings.preferences.violence_level;
    gPreferencesTargetHighlight1 = settings.preferences.target_highlight;
    gPreferencesCombatMessages1 = settings.preferences.combat_messages;
    gPreferencesCombatLooks1 = settings.preferences.combat_looks;
    gPreferencesCombatTaunts1 = settings.preferences.combat_taunts;
    gPreferencesLanguageFilter1 = settings.preferences.language_filter;
    gPreferencesRunning1 = settings.preferences.running;
    gPreferencesSubtitles1 = settings.preferences.subtitles;
    gPreferencesItemHighlight1 = settings.preferences.item_highlight;
    gPreferencesCombatSpeed1 = settings.preferences.combat_speed;
    gPreferencesTextBaseDelay1 = settings.preferences.text_base_delay;
    gPreferencesPlayerSpeedup1 = settings.preferences.player_speedup;
    gPreferencesMasterVolume1 = settings.sound.master_volume;
    gPreferencesMusicVolume1 = settings.sound.music_volume;
    gPreferencesSoundEffectsVolume1 = settings.sound.sndfx_volume;
    gPreferencesSpeechVolume1 = settings.sound.speech_volume;
    gPreferencesBrightness1 = settings.preferences.brightness;
    gPreferencesMouseSensitivity1 = settings.preferences.mouse_sensitivity;

    _JustUpdate_();
}

// 0x493054
static void _SaveSettings()
{
    gPreferencesGameDifficulty2 = gPreferencesGameDifficulty1;
    gPreferencesCombatDifficulty2 = gPreferencesCombatDifficulty1;
    gPreferencesViolenceLevel2 = gPreferencesViolenceLevel1;
    gPreferencesTargetHighlight2 = gPreferencesTargetHighlight1;
    gPreferencesCombatLooks2 = gPreferencesCombatLooks1;
    gPreferencesCombatMessages2 = gPreferencesCombatMessages1;
    gPreferencesCombatTaunts2 = gPreferencesCombatTaunts1;
    gPreferencesLanguageFilter2 = gPreferencesLanguageFilter1;
    gPreferencesRunning2 = gPreferencesRunning1;
    gPreferencesSubtitles2 = gPreferencesSubtitles1;
    gPreferencesItemHighlight2 = gPreferencesItemHighlight1;
    gPreferencesCombatSpeed2 = gPreferencesCombatSpeed1;
    gPreferencesPlayerSpeedup2 = gPreferencesPlayerSpeedup1;
    gPreferencesMasterVolume2 = gPreferencesMasterVolume1;
    gPreferencesTextBaseDelay2 = gPreferencesTextBaseDelay1;
    gPreferencesMusicVolume2 = gPreferencesMusicVolume1;
    gPreferencesBrightness2 = gPreferencesBrightness1;
    gPreferencesSoundEffectsVolume2 = gPreferencesSoundEffectsVolume1;
    gPreferencesMouseSensitivity2 = gPreferencesMouseSensitivity1;
    gPreferencesSpeechVolume2 = gPreferencesSpeechVolume1;
}

// 0x493128
static void _RestoreSettings()
{
    gPreferencesGameDifficulty1 = gPreferencesGameDifficulty2;
    gPreferencesCombatDifficulty1 = gPreferencesCombatDifficulty2;
    gPreferencesViolenceLevel1 = gPreferencesViolenceLevel2;
    gPreferencesTargetHighlight1 = gPreferencesTargetHighlight2;
    gPreferencesCombatLooks1 = gPreferencesCombatLooks2;
    gPreferencesCombatMessages1 = gPreferencesCombatMessages2;
    gPreferencesCombatTaunts1 = gPreferencesCombatTaunts2;
    gPreferencesLanguageFilter1 = gPreferencesLanguageFilter2;
    gPreferencesRunning1 = gPreferencesRunning2;
    gPreferencesSubtitles1 = gPreferencesSubtitles2;
    gPreferencesItemHighlight1 = gPreferencesItemHighlight2;
    gPreferencesCombatSpeed1 = gPreferencesCombatSpeed2;
    gPreferencesPlayerSpeedup1 = gPreferencesPlayerSpeedup2;
    gPreferencesMasterVolume1 = gPreferencesMasterVolume2;
    gPreferencesTextBaseDelay1 = gPreferencesTextBaseDelay2;
    gPreferencesMusicVolume1 = gPreferencesMusicVolume2;
    gPreferencesBrightness1 = gPreferencesBrightness2;
    gPreferencesSoundEffectsVolume1 = gPreferencesSoundEffectsVolume2;
    gPreferencesMouseSensitivity1 = gPreferencesMouseSensitivity2;
    gPreferencesSpeechVolume1 = gPreferencesSpeechVolume2;

    _JustUpdate_();
}

// 0x492F60
static void preferencesSetDefaults(bool a1)
{
    gPreferencesCombatDifficulty1 = COMBAT_DIFFICULTY_NORMAL;
    gPreferencesViolenceLevel1 = VIOLENCE_LEVEL_MAXIMUM_BLOOD;
    gPreferencesTargetHighlight1 = TARGET_HIGHLIGHT_TARGETING_ONLY;
    gPreferencesCombatMessages1 = 1;
    gPreferencesCombatLooks1 = 0;
    gPreferencesCombatTaunts1 = 1;
    gPreferencesRunning1 = 0;
    gPreferencesSubtitles1 = 0;
    gPreferencesItemHighlight1 = 1;
    gPreferencesCombatSpeed1 = 0;
    gPreferencesPlayerSpeedup1 = 0;
    gPreferencesTextBaseDelay1 = 3.5;
    gPreferencesBrightness1 = 1.0;
    gPreferencesMouseSensitivity1 = 1.0;
    gPreferencesGameDifficulty1 = 1;
    gPreferencesLanguageFilter1 = 0;
    gPreferencesMasterVolume1 = 22281;
    gPreferencesMusicVolume1 = 22281;
    gPreferencesSoundEffectsVolume1 = 22281;
    gPreferencesSpeechVolume1 = 22281;

    if (a1) {
        for (int index = 0; index < PREF_COUNT; index++) {
            _UpdateThing(index);
        }
        _win_set_button_rest_state(_plyrspdbid, gPreferencesPlayerSpeedup1, 0);
        windowRefresh(gPreferencesWindow);
        _changed = true;
    }
}

// 0x4931F8
static void _JustUpdate_()
{
    gPreferencesGameDifficulty1 = std::clamp(gPreferencesGameDifficulty1, 0, 2);
    gPreferencesCombatDifficulty1 = std::clamp(gPreferencesCombatDifficulty1, 0, 2);
    gPreferencesViolenceLevel1 = std::clamp(gPreferencesViolenceLevel1, 0, 3);
    gPreferencesTargetHighlight1 = std::clamp(gPreferencesTargetHighlight1, 0, 2);
    gPreferencesCombatMessages1 = std::clamp(gPreferencesCombatMessages1, 0, 1);
    gPreferencesCombatLooks1 = std::clamp(gPreferencesCombatLooks1, 0, 1);
    gPreferencesCombatTaunts1 = std::clamp(gPreferencesCombatTaunts1, 0, 1);
    gPreferencesLanguageFilter1 = std::clamp(gPreferencesLanguageFilter1, 0, 1);
    gPreferencesRunning1 = std::clamp(gPreferencesRunning1, 0, 1);
    gPreferencesSubtitles1 = std::clamp(gPreferencesSubtitles1, 0, 1);
    gPreferencesItemHighlight1 = std::clamp(gPreferencesItemHighlight1, 0, 1);
    gPreferencesCombatSpeed1 = std::clamp(gPreferencesCombatSpeed1, 0, 50);
    gPreferencesPlayerSpeedup1 = std::clamp(gPreferencesPlayerSpeedup1, 0, 1);
    gPreferencesTextBaseDelay1 = std::clamp(gPreferencesTextBaseDelay1, 6.0, 10.0);
    gPreferencesMasterVolume1 = std::clamp(gPreferencesMasterVolume1, 0, VOLUME_MAX);
    gPreferencesMusicVolume1 = std::clamp(gPreferencesMusicVolume1, 0, VOLUME_MAX);
    gPreferencesSoundEffectsVolume1 = std::clamp(gPreferencesSoundEffectsVolume1, 0, VOLUME_MAX);
    gPreferencesSpeechVolume1 = std::clamp(gPreferencesSpeechVolume1, 0, VOLUME_MAX);
    gPreferencesBrightness1 = std::clamp(gPreferencesBrightness1, 1.0, 1.17999267578125);
    gPreferencesMouseSensitivity1 = std::clamp(gPreferencesMouseSensitivity1, 1.0, 2.5);

    textObjectsSetBaseDelay(gPreferencesTextBaseDelay1);
    gameMouseLoadItemHighlight();

    double textLineDelay = (gPreferencesTextBaseDelay1 + (-1.0)) * 0.2 * 2.0;
    textLineDelay = std::clamp(textLineDelay, 0.0, 2.0);

    textObjectsSetLineDelay(textLineDelay);
    aiMessageListReloadIfNeeded();
    _scr_message_free();
    gameSoundSetMasterVolume(gPreferencesMasterVolume1);
    backgroundSoundSetVolume(gPreferencesMusicVolume1);
    soundEffectsSetVolume(gPreferencesSoundEffectsVolume1);
    speechSetVolume(gPreferencesSpeechVolume1);
    mouseSetSensitivity(gPreferencesMouseSensitivity1);
    colorSetBrightness(gPreferencesBrightness1);
}

// 0x491A68
static void _UpdateThing(int index)
{
    fontSetCurrent(101);

    PreferenceDescription* meta = &(gPreferenceDescriptions[index]);

    if (index >= FIRST_PRIMARY_PREF && index <= LAST_PRIMARY_PREF) {
        int primaryOptionIndex = index - FIRST_PRIMARY_PREF;

        int offsets[PRIMARY_PREF_COUNT];
        memcpy(offsets, dword_48FC1C, sizeof(dword_48FC1C));

        blitBufferToBuffer(
            cleanPreferencesBuffer + scaledWidth * static_cast<int>(offsets[primaryOptionIndex] * scaleY) + static_cast<int>(23 * scaleX),
            static_cast<int>(160 * scaleX),
            static_cast<int>(54 * scaleY),
            scaledWidth,
            gPreferencesWindowBuffer + scaledWidth * static_cast<int>(offsets[primaryOptionIndex] * scaleY) + static_cast<int>(23 * scaleX),
            scaledWidth);

        for (int valueIndex = 0; valueIndex < meta->valuesCount; valueIndex++) {
            const char* text = getmsg(&gPreferencesMessageList, &gPreferencesMessageListItem, meta->labelIds[valueIndex]);

            char copy[100]; // TODO: Size is probably wrong.
            strcpy(copy, text);

            int x = meta->knobX + word_48FBF6[valueIndex];
            int len = fontGetStringWidth(copy);
            switch (valueIndex) {
            case 0:
                x -= fontGetStringWidth(copy);
                meta->minX = x;
                break;
            case 1:
                x -= len / 2;
                meta->maxX = x + len;
                break;
            case 2:
            case 3:
                meta->maxX = x + len;
                break;
            }

            char* p = copy;
            while (*p != '\0' && *p != ' ') {
                p++;
            }

            int y = meta->knobY + word_48FBFE[valueIndex];
            const char* s;
            if (*p != '\0') {
                *p = '\0';
                fontDrawText(compositeBuffer + 640 * y + x, copy, 640, 640, _colorTable[18979]);
                s = p + 1;
                y += fontGetLineHeight();
            } else {
                s = copy;
            }

            fontDrawText(compositeBuffer + 640 * y + x, s, 640, 640, _colorTable[18979]);
        }

        int value = *(meta->valuePtr);

        blitBufferToBufferTrans(
            gPrimarySwitchStretched + (static_cast<int>(46 * scaleX) * static_cast<int>(47 * scaleY)) * value,
            static_cast<int>(46 * scaleX),
            static_cast<int>(47 * scaleY),
            gPrimarySwitchStretchedWidth,
            gPreferencesWindowBuffer + scaledWidth * static_cast<int>(meta->knobY * scaleY) + static_cast<int>(meta->knobX * scaleX),
            scaledWidth);

    } else if (index >= FIRST_SECONDARY_PREF && index <= LAST_SECONDARY_PREF) {
        int secondaryOptionIndex = index - FIRST_SECONDARY_PREF;

        int offsets[SECONDARY_PREF_COUNT];
        memcpy(offsets, dword_48FC30, sizeof(dword_48FC30));

        blitBufferToBuffer(
            cleanPreferencesBuffer + scaledWidth * static_cast<int>(offsets[secondaryOptionIndex] * scaleY) + static_cast<int>(251 * scaleX),
            static_cast<int>(113 * scaleX),
            static_cast<int>(34 * scaleY),
            scaledWidth,
            gPreferencesWindowBuffer + scaledWidth * static_cast<int>(offsets[secondaryOptionIndex] * scaleY) + static_cast<int>(251 * scaleX),
            scaledWidth);

        // Secondary options are booleans, so it's index is also it's value.
        for (int value = 0; value < 2; value++) {
            const char* text = getmsg(&gPreferencesMessageList, &gPreferencesMessageListItem, meta->labelIds[value]);

            int x;
            if (value) {
                x = meta->knobX + word_48FC06[value];
                meta->maxX = x + fontGetStringWidth(text);
            } else {
                x = meta->knobX + word_48FC06[value] - fontGetStringWidth(text);
                meta->minX = x;
            }
            fontDrawText(compositeBuffer + 640 * (meta->knobY - 5) + x, text, 640, 640, _colorTable[18979]);
        }

        int value = *(meta->valuePtr);
        if (index == PREF_COMBAT_MESSAGES) {
            value ^= 1;
        }

        blitBufferToBufferTrans(
            gSecondarySwitchStretched + (static_cast<int>(22 * scaleX) * static_cast<int>(25 * scaleY)) * value,
            static_cast<int>(22 * scaleX),
            static_cast<int>(25 * scaleY),
            gSecondarySwitchStretchedWidth,
            gPreferencesWindowBuffer + scaledWidth * static_cast<int>(meta->knobY * scaleY) + static_cast<int>(meta->knobX * scaleX),
            scaledWidth);

    } else if (index >= FIRST_RANGE_PREF && index <= LAST_RANGE_PREF) {

        blitBufferToBuffer(
            cleanPreferencesBuffer + scaledWidth * static_cast<int>((meta->knobY - 12) * scaleY) + static_cast<int>(384 * scaleX),
            static_cast<int>(240 * scaleX),
            static_cast<int>(24 * scaleY),
            scaledWidth,
            gPreferencesWindowBuffer + scaledWidth * static_cast<int>((meta->knobY - 12) * scaleY) + static_cast<int>(384 * scaleX),
            scaledWidth);

        switch (index) {
        case PREF_COMBAT_SPEED:
            if (1) {
                double value = *meta->valuePtr;
                value = std::clamp(value, 0.0, 50.0);

                int x = (int)((value - meta->minValue) * 219.0 / (meta->maxValue - meta->minValue) + 384.0);
                blitBufferToBufferTrans(
                    gKnobOffImageStretched, // Source buffer: stretched knob "off" image
                    static_cast<int>(21 * scaleX), // Source width after scaling (original width = 21px)
                    static_cast<int>(11 * scaleY), // Source height after scaling (original height = 12px) — trimmed to avoid stretched glitch at bottom
                    gKnobStretchedWidth, // Source pitch (bytes per row) — matches the full stretched image row width
                    gPreferencesWindowBuffer + // Destination buffer: main preferences window
                        scaledWidth * (static_cast<int>(meta->knobY * scaleY) // Y offset in destination, scaled
                            + static_cast<int>(1 * scaleY) // Extra 1px vertical shift to clip glitchy bottom row
                            )
                        + static_cast<int>(x * scaleX), // X offset in destination, scaled
                    scaledWidth // Destination pitch (bytes per row)
                );
            }
            break;
        case PREF_TEXT_BASE_DELAY:
            if (1) {
                gPreferencesTextBaseDelay1 = std::clamp(gPreferencesTextBaseDelay1, 1.0, 6.0);

                int x = (int)((6.0 - gPreferencesTextBaseDelay1) * 43.8 + 384.0);
                blitBufferToBufferTrans(
                    gKnobOffImageStretched, // Source buffer: stretched knob "off" image
                    static_cast<int>(21 * scaleX), // Source width after scaling (original width = 21px)
                    static_cast<int>(11 * scaleY), // Source height after scaling (original height = 12px) — trimmed to avoid stretched glitch at bottom
                    gKnobStretchedWidth, // Source pitch (bytes per row) — matches the full stretched image row width
                    gPreferencesWindowBuffer + // Destination buffer: main preferences window
                        scaledWidth * (static_cast<int>(meta->knobY * scaleY) // Y offset in destination, scaled
                            + static_cast<int>(1 * scaleY) // Extra 1px vertical shift to clip glitchy bottom row
                            )
                        + static_cast<int>(x * scaleX), // X offset in destination, scaled
                    scaledWidth // Destination pitch (bytes per row)
                );

                double value = (gPreferencesTextBaseDelay1 - 1.0) * 0.2 * 2.0;
                value = std::clamp(value, 0.0, 2.0);

                textObjectsSetBaseDelay(gPreferencesTextBaseDelay1);
                textObjectsSetLineDelay(value);
            }
            break;
        case PREF_MASTER_VOLUME:
        case PREF_MUSIC_VOLUME:
        case PREF_SFX_VOLUME:
        case PREF_SPEECH_VOLUME: {
            double value = *meta->valuePtr;
            value = std::clamp(value, meta->minValue, meta->maxValue);

            int x = (int)((value - meta->minValue) * 219.0 / (meta->maxValue - meta->minValue) + 384.0);
            x = std::clamp(x, 0, 640 - 21);

            int y = std::clamp(static_cast<int>(meta->knobY), 0, 480 - 12);

            blitBufferToBufferTrans(
                gKnobOffImageStretched,
                static_cast<int>(21 * scaleX),
                static_cast<int>(11 * scaleY),
                gKnobStretchedWidth,
                gPreferencesWindowBuffer + scaledWidth * (static_cast<int>(meta->knobY * scaleY) + static_cast<int>(1 * scaleY)) + static_cast<int>(x * scaleX),
                scaledWidth);

            switch (index) {
            case PREF_MASTER_VOLUME:
                gameSoundSetMasterVolume(gPreferencesMasterVolume1);
                break;
            case PREF_MUSIC_VOLUME:
                backgroundSoundSetVolume(gPreferencesMusicVolume1);
                break;
            case PREF_SFX_VOLUME:
                soundEffectsSetVolume(gPreferencesSoundEffectsVolume1);
                break;
            case PREF_SPEECH_VOLUME:
                speechSetVolume(gPreferencesSpeechVolume1);
                break;
            }
        } break;
        case PREF_BRIGHTNESS:
            if (1) {
                gPreferencesBrightness1 = std::clamp(gPreferencesBrightness1, 1.0, 1.17999267578125);

                int x = (int)((gPreferencesBrightness1 - meta->minValue) * (219.0 / (meta->maxValue - meta->minValue)) + 384.0);
                blitBufferToBufferTrans(
                    gKnobOffImageStretched, // Source buffer: stretched knob "off" image
                    static_cast<int>(21 * scaleX), // Source width after scaling (original width = 21px)
                    static_cast<int>(11 * scaleY), // Source height after scaling (original height = 12px) — trimmed to avoid stretched glitch at bottom
                    gKnobStretchedWidth, // Source pitch (bytes per row) — matches the full stretched image row width
                    gPreferencesWindowBuffer + // Destination buffer: main preferences window
                        scaledWidth * (static_cast<int>(meta->knobY * scaleY) // Y offset in destination, scaled
                            + static_cast<int>(1 * scaleY) // Extra 1px vertical shift to clip glitchy bottom row
                            )
                        + static_cast<int>(x * scaleX), // X offset in destination, scaled
                    scaledWidth // Destination pitch (bytes per row)
                );

                colorSetBrightness(gPreferencesBrightness1);
            }
            break;
        case PREF_MOUSE_SENSITIVIY:
            if (1) {
                gPreferencesMouseSensitivity1 = std::clamp(gPreferencesMouseSensitivity1, 1.0, 2.5);

                int x = (int)((gPreferencesMouseSensitivity1 - meta->minValue) * (219.0 / (meta->maxValue - meta->minValue)) + 384.0);
                blitBufferToBufferTrans(
                    gKnobOffImageStretched, // Source buffer: stretched knob "off" image
                    static_cast<int>(21 * scaleX), // Source width after scaling (original width = 21px)
                    static_cast<int>(11 * scaleY), // Source height after scaling (original height = 12px) — trimmed to avoid stretched glitch at bottom
                    gKnobStretchedWidth, // Source pitch (bytes per row) — matches the full stretched image row width
                    gPreferencesWindowBuffer + // Destination buffer: main preferences window
                        scaledWidth * (static_cast<int>(meta->knobY * scaleY) // Y offset in destination, scaled
                            + static_cast<int>(1 * scaleY) // Extra 1px vertical shift to clip glitchy bottom row
                            )
                        + static_cast<int>(x * scaleX), // X offset in destination, scaled
                    scaledWidth // Destination pitch (bytes per row)
                );

                mouseSetSensitivity(gPreferencesMouseSensitivity1);
            }
            break;
        }

        for (int optionIndex = 0; optionIndex < meta->valuesCount; optionIndex++) {
            const char* str = getmsg(&gPreferencesMessageList, &gPreferencesMessageListItem, meta->labelIds[optionIndex]);

            int x;
            switch (optionIndex) {
            case 0:
                // 0x4926AA
                x = 384;
                // TODO: Incomplete.
                break;
            case 1:
                // 0x4926F3
                switch (meta->valuesCount) {
                case 2:
                    x = 624 - fontGetStringWidth(str);
                    break;
                case 3:
                    // This code path does not use floating-point arithmetic
                    x = 504 - fontGetStringWidth(str) / 2 - 2;
                    break;
                case 4:
                    // Uses floating-point arithmetic
                    x = 444 + fontGetStringWidth(str) / 2 - 8;
                    break;
                }
                break;
            case 2:
                // 0x492766
                switch (meta->valuesCount) {
                case 3:
                    x = 624 - fontGetStringWidth(str);
                    break;
                case 4:
                    // Uses floating-point arithmetic
                    x = 564 - fontGetStringWidth(str) - 4;
                    break;
                }
                break;
            case 3:
                // 0x49279E
                x = 624 - fontGetStringWidth(str);
                break;
            }
            fontDrawText(compositeBuffer + 640 * (meta->knobY - 12) + x, str, 640, 640, _colorTable[18979]);
        }
    } else {
        // return false;
    }

    // TODO: Incomplete.

    // return true;
}

// 0x492CB0
int _SavePrefs(bool save)
{
    settings.preferences.game_difficulty = gPreferencesGameDifficulty1;
    settings.preferences.combat_difficulty = gPreferencesCombatDifficulty1;
    settings.preferences.violence_level = gPreferencesViolenceLevel1;
    settings.preferences.target_highlight = gPreferencesTargetHighlight1;
    settings.preferences.combat_messages = gPreferencesCombatMessages1;
    settings.preferences.combat_looks = gPreferencesCombatLooks1;
    settings.preferences.combat_taunts = gPreferencesCombatTaunts1;
    settings.preferences.language_filter = gPreferencesLanguageFilter1;
    settings.preferences.running = gPreferencesRunning1;
    settings.preferences.subtitles = gPreferencesSubtitles1;
    settings.preferences.item_highlight = gPreferencesItemHighlight1;
    settings.preferences.combat_speed = gPreferencesCombatSpeed1;
    settings.preferences.text_base_delay = gPreferencesTextBaseDelay1;

    double textLineDelay = (gPreferencesTextBaseDelay1 + dbl_50C2D0) * dbl_50C2D8 * dbl_50C2E0;
    if (textLineDelay >= 0.0) {
        if (textLineDelay > dbl_50C2E0) {
            textLineDelay = 2.0;
        }

        settings.preferences.text_line_delay = textLineDelay;
    } else {
        settings.preferences.text_line_delay = 0.0;
    }

    settings.preferences.player_speedup = gPreferencesPlayerSpeedup1;
    settings.sound.master_volume = gPreferencesMasterVolume1;
    settings.sound.music_volume = gPreferencesMusicVolume1;
    settings.sound.sndfx_volume = gPreferencesSoundEffectsVolume1;
    settings.sound.speech_volume = gPreferencesSpeechVolume1;

    settings.preferences.brightness = gPreferencesBrightness1;
    settings.preferences.mouse_sensitivity = gPreferencesMouseSensitivity1;

    if (save) {
        settingsSave();
    }

    return 0;
}

// 0x493224
int preferencesSave(File* stream)
{
    float textBaseDelay = (float)gPreferencesTextBaseDelay1;
    float brightness = (float)gPreferencesBrightness1;
    float mouseSensitivity = (float)gPreferencesMouseSensitivity1;

    if (fileWriteInt32(stream, gPreferencesGameDifficulty1) == -1)
        goto err;
    if (fileWriteInt32(stream, gPreferencesCombatDifficulty1) == -1)
        goto err;
    if (fileWriteInt32(stream, gPreferencesViolenceLevel1) == -1)
        goto err;
    if (fileWriteInt32(stream, gPreferencesTargetHighlight1) == -1)
        goto err;
    if (fileWriteInt32(stream, gPreferencesCombatLooks1) == -1)
        goto err;
    if (fileWriteInt32(stream, gPreferencesCombatMessages1) == -1)
        goto err;
    if (fileWriteInt32(stream, gPreferencesCombatTaunts1) == -1)
        goto err;
    if (fileWriteInt32(stream, gPreferencesLanguageFilter1) == -1)
        goto err;
    if (fileWriteInt32(stream, gPreferencesRunning1) == -1)
        goto err;
    if (fileWriteInt32(stream, gPreferencesSubtitles1) == -1)
        goto err;
    if (fileWriteInt32(stream, gPreferencesItemHighlight1) == -1)
        goto err;
    if (fileWriteInt32(stream, gPreferencesCombatSpeed1) == -1)
        goto err;
    if (fileWriteInt32(stream, gPreferencesPlayerSpeedup1) == -1)
        goto err;
    if (fileWriteFloat(stream, textBaseDelay) == -1)
        goto err;
    if (fileWriteInt32(stream, gPreferencesMasterVolume1) == -1)
        goto err;
    if (fileWriteInt32(stream, gPreferencesMusicVolume1) == -1)
        goto err;
    if (fileWriteInt32(stream, gPreferencesSoundEffectsVolume1) == -1)
        goto err;
    if (fileWriteInt32(stream, gPreferencesSpeechVolume1) == -1)
        goto err;
    if (fileWriteFloat(stream, brightness) == -1)
        goto err;
    if (fileWriteFloat(stream, mouseSensitivity) == -1)
        goto err;

    return 0;

err:

    debugPrint("\nOPTION MENU: Error save option data!\n");

    return -1;
}

// 0x49340C
int preferencesLoad(File* stream)
{
    float textBaseDelay;
    float brightness;
    float mouseSensitivity;

    preferencesSetDefaults(false);

    if (fileReadInt32(stream, &gPreferencesGameDifficulty1) == -1)
        goto err;
    if (fileReadInt32(stream, &gPreferencesCombatDifficulty1) == -1)
        goto err;
    if (fileReadInt32(stream, &gPreferencesViolenceLevel1) == -1)
        goto err;
    if (fileReadInt32(stream, &gPreferencesTargetHighlight1) == -1)
        goto err;
    if (fileReadInt32(stream, &gPreferencesCombatLooks1) == -1)
        goto err;
    if (fileReadInt32(stream, &gPreferencesCombatMessages1) == -1)
        goto err;
    if (fileReadInt32(stream, &gPreferencesCombatTaunts1) == -1)
        goto err;
    if (fileReadInt32(stream, &gPreferencesLanguageFilter1) == -1)
        goto err;
    if (fileReadInt32(stream, &gPreferencesRunning1) == -1)
        goto err;
    if (fileReadInt32(stream, &gPreferencesSubtitles1) == -1)
        goto err;
    if (fileReadInt32(stream, &gPreferencesItemHighlight1) == -1)
        goto err;
    if (fileReadInt32(stream, &gPreferencesCombatSpeed1) == -1)
        goto err;
    if (fileReadInt32(stream, &gPreferencesPlayerSpeedup1) == -1)
        goto err;
    if (fileReadFloat(stream, &textBaseDelay) == -1)
        goto err;
    if (fileReadInt32(stream, &gPreferencesMasterVolume1) == -1)
        goto err;
    if (fileReadInt32(stream, &gPreferencesMusicVolume1) == -1)
        goto err;
    if (fileReadInt32(stream, &gPreferencesSoundEffectsVolume1) == -1)
        goto err;
    if (fileReadInt32(stream, &gPreferencesSpeechVolume1) == -1)
        goto err;
    if (fileReadFloat(stream, &brightness) == -1)
        goto err;
    if (fileReadFloat(stream, &mouseSensitivity) == -1)
        goto err;

    gPreferencesBrightness1 = brightness;
    gPreferencesMouseSensitivity1 = mouseSensitivity;
    gPreferencesTextBaseDelay1 = textBaseDelay;

    _JustUpdate_();
    _SavePrefs(0);

    return 0;

err:

    debugPrint("\nOPTION MENU: Error loading option data!, using defaults.\n");

    preferencesSetDefaults(false);
    _JustUpdate_();
    _SavePrefs(0);

    return -1;
}

// 0x4928E4
void brightnessIncrease()
{
    gPreferencesBrightness1 = settings.preferences.brightness;

    if (gPreferencesBrightness1 < dbl_50C168) {
        gPreferencesBrightness1 += dbl_50C170;

        if (gPreferencesBrightness1 >= 1.0) {
            if (gPreferencesBrightness1 > dbl_50C168) {
                gPreferencesBrightness1 = dbl_50C168;
            }
        } else {
            gPreferencesBrightness1 = 1.0;
        }

        colorSetBrightness(gPreferencesBrightness1);

        settings.preferences.brightness = gPreferencesBrightness1;

        settingsSave();
    }
}

// 0x4929C8
void brightnessDecrease()
{
    gPreferencesBrightness1 = settings.preferences.brightness;

    if (gPreferencesBrightness1 > 1.0) {
        gPreferencesBrightness1 += dbl_50C178;

        if (gPreferencesBrightness1 >= 1.0) {
            if (gPreferencesBrightness1 > dbl_50C180) {
                gPreferencesBrightness1 = dbl_50C180;
            }
        } else {
            gPreferencesBrightness1 = 1.0;
        }

        colorSetBrightness(gPreferencesBrightness1);

        settings.preferences.brightness = gPreferencesBrightness1;

        settingsSave();
    }
}

static FrmImage _preferencesBackgroundFrmImage;

// 0x4908A0
static int preferencesWindowInit()
{
    int i;
    int fid;
    char* messageItemText;
    int x;
    int y;
    int width;
    int height;
    int messageItemId;
    int btn;

    if (!messageListInit(&gPreferencesMessageList)) {
        return -1;
    }

    char path[COMPAT_MAX_PATH];
    snprintf(path, sizeof(path), "%s%s", asc_5186C8, "options.msg");
    if (!messageListLoad(&gPreferencesMessageList, path)) {
        return -1;
    }

    _oldFont = fontGetCurrent();

    _SaveSettings();

    for (i = 0; i < PREFERENCES_WINDOW_FRM_COUNT; i++) {
        fid = buildFid(OBJ_TYPE_INTERFACE, gPreferencesWindowFrmIds[i], 0, 0, 0);
        if (!_preferencesFrmImages[i].lock(fid)) {
            while (--i >= 0) {
                _preferencesFrmImages[i].unlock();
            }
            return -1;
        }
    }

    // Load stretch mode from INI file
    int menuStretchMode = 0; // Default to 0, no stretch
    int stretchGameMode = 0;
    Config config;
    if (configInit(&config)) {
        if (configRead(&config, "f2_res.ini", false)) {
            configGetInt(&config, "STATIC_SCREENS", "PREFERENCES_SIZE", &menuStretchMode);

            if (configGetInt(&config, "STATIC_SCREENS", "STRETCH_GAME", &stretchGameMode)) {
                menuStretchMode = stretchGameMode; // always override if key exists
            }
        }
        configFree(&config);
    }

    // Get the original image dimensions
    originalWidth = PREFERENCES_WINDOW_WIDTH;
    originalHeight = PREFERENCES_WINDOW_HEIGHT;

    scaledWidth = originalWidth;
    scaledHeight = originalHeight;

    // Figure out how to stretch the preferences depending on resolution + settings
    calculateScaledSize(
        originalWidth,
        originalHeight,
        screenWidth,
        screenHeight,
        menuStretchMode,
        scaledWidth,
        scaledHeight
    );

    _changed = false;

    // Center the menu window on screen
    int preferencesWindowX = (screenGetWidth() - scaledWidth) / 2;
    int preferencesWindowY = (screenGetHeight() - scaledHeight) / 2;

    // Create the Preferences window after the stretching calculations
    gPreferencesWindow = windowCreate(preferencesWindowX,
        preferencesWindowY,
        scaledWidth,
        scaledHeight,
        256,
        WINDOW_MODAL | WINDOW_DONT_MOVE_TOP);
    if (gPreferencesWindow == -1) {
        for (i = 0; i < PREFERENCES_WINDOW_FRM_COUNT; i++) {
            _preferencesFrmImages[i].unlock();
        }
        return preferences_fatal_error();
    }

    gPreferencesWindowBuffer = windowGetBuffer(gPreferencesWindow);

    // Load the background image
    int backgroundFid = buildFid(OBJ_TYPE_INTERFACE, 240, 0, 0, 0);
    if (!_preferencesBackgroundFrmImage.lock(backgroundFid)) {
        return preferences_fatal_error();
    }

    unsigned char* backgroundData = _preferencesBackgroundFrmImage.getData();

    // Clone the background so we can draw text on it (before scaling)
    compositeBuffer = (uint8_t*)SDL_malloc(originalWidth * originalHeight);
    if (!compositeBuffer) {
        return preferences_fatal_error();
    }

    // Allocate the clean buffer
    cleanPreferencesBuffer = (uint8_t*)SDL_malloc(scaledWidth * scaledHeight);
    if (!cleanPreferencesBuffer) {
        SDL_free(gPreferencesWindowBuffer);
        return preferences_fatal_error();
    }

    memcpy(compositeBuffer, backgroundData, originalWidth * originalHeight);

    fontSetCurrent(104);

    messageItemText = getmsg(&gPreferencesMessageList, &gPreferencesMessageListItem, 100);
    fontDrawText(compositeBuffer + PREFERENCES_WINDOW_WIDTH * 10 + 74, messageItemText, PREFERENCES_WINDOW_WIDTH, PREFERENCES_WINDOW_WIDTH, _colorTable[18979]);

    fontSetCurrent(103);

    messageItemId = 101;
    for (i = 0; i < PRIMARY_PREF_COUNT; i++) {
        messageItemText = getmsg(&gPreferencesMessageList, &gPreferencesMessageListItem, messageItemId++);
        x = 99 - fontGetStringWidth(messageItemText) / 2;
        fontDrawText(compositeBuffer + PREFERENCES_WINDOW_WIDTH * _row1Ytab[i] + x, messageItemText, PREFERENCES_WINDOW_WIDTH, PREFERENCES_WINDOW_WIDTH, _colorTable[18979]);
    }

    for (i = 0; i < SECONDARY_PREF_COUNT; i++) {
        messageItemText = getmsg(&gPreferencesMessageList, &gPreferencesMessageListItem, messageItemId++);
        fontDrawText(compositeBuffer + PREFERENCES_WINDOW_WIDTH * _row2Ytab[i] + 206, messageItemText, PREFERENCES_WINDOW_WIDTH, PREFERENCES_WINDOW_WIDTH, _colorTable[18979]);
    }

    for (i = 0; i < RANGE_PREF_COUNT; i++) {
        messageItemText = getmsg(&gPreferencesMessageList, &gPreferencesMessageListItem, messageItemId++);
        fontDrawText(compositeBuffer + PREFERENCES_WINDOW_WIDTH * _row3Ytab[i] + 384, messageItemText, PREFERENCES_WINDOW_WIDTH, PREFERENCES_WINDOW_WIDTH, _colorTable[18979]);
    }

    // DEFAULT
    messageItemText = getmsg(&gPreferencesMessageList, &gPreferencesMessageListItem, 120);
    fontDrawText(compositeBuffer + PREFERENCES_WINDOW_WIDTH * 449 + 43, messageItemText, PREFERENCES_WINDOW_WIDTH, PREFERENCES_WINDOW_WIDTH, _colorTable[18979]);

    // DONE
    messageItemText = getmsg(&gPreferencesMessageList, &gPreferencesMessageListItem, 4);
    fontDrawText(compositeBuffer + PREFERENCES_WINDOW_WIDTH * 449 + 169, messageItemText, PREFERENCES_WINDOW_WIDTH, PREFERENCES_WINDOW_WIDTH, _colorTable[18979]);

    // CANCEL
    messageItemText = getmsg(&gPreferencesMessageList, &gPreferencesMessageListItem, 121);
    fontDrawText(compositeBuffer + PREFERENCES_WINDOW_WIDTH * 449 + 283, messageItemText, PREFERENCES_WINDOW_WIDTH, PREFERENCES_WINDOW_WIDTH, _colorTable[18979]);

    // Affect player speed
    fontSetCurrent(101);
    messageItemText = getmsg(&gPreferencesMessageList, &gPreferencesMessageListItem, 122);
    fontDrawText(compositeBuffer + PREFERENCES_WINDOW_WIDTH * 72 + 405, messageItemText, PREFERENCES_WINDOW_WIDTH, PREFERENCES_WINDOW_WIDTH, _colorTable[18979]);

    // Base image dimensions
    int buttonBaseWidth = _preferencesFrmImages[PREFERENCES_WINDOW_FRM_LITTLE_RED_BUTTON_UP].getWidth();
    int buttonBaseHeight = _preferencesFrmImages[PREFERENCES_WINDOW_FRM_LITTLE_RED_BUTTON_UP].getHeight();
    int toggleBaseWidth = _preferencesFrmImages[PREFERENCES_WINDOW_FRM_CHECKBOX_OFF].getWidth();
    int toggleBaseHeight = _preferencesFrmImages[PREFERENCES_WINDOW_FRM_CHECKBOX_OFF].getHeight();

    // Y-scaling for round shape consistency
    scaleX = static_cast<float>(scaledWidth) / originalWidth;
    scaleY = static_cast<float>(scaledHeight) / originalHeight;
    float buttonScale = scaleY;

    // Stretch other buttons here
    prepareStretchedKnob(scaleX, scaleY);
    prepareStretchedButtons(scaleX, scaleY);

    int buttonWidth = static_cast<int>(buttonBaseWidth * buttonScale);
    int buttonHeight = static_cast<int>(buttonBaseHeight * buttonScale);
    int toggleWidth = static_cast<int>(toggleBaseWidth * buttonScale);
    int toggleHeight = static_cast<int>(toggleBaseHeight * buttonScale);

    // Allocate memory
    unsigned char* scaledNormal = reinterpret_cast<unsigned char*>(SDL_malloc(buttonWidth * buttonHeight));
    unsigned char* scaledPressed = reinterpret_cast<unsigned char*>(SDL_malloc(buttonWidth * buttonHeight));
    unsigned char* toggleNormal = reinterpret_cast<unsigned char*>(SDL_malloc(toggleWidth * toggleHeight));
    unsigned char* togglePressed = reinterpret_cast<unsigned char*>(SDL_malloc(toggleWidth * toggleHeight));

    if (!scaledNormal || !scaledPressed || !toggleNormal || !togglePressed) {
        return preferences_fatal_error();
    }

    // Scale red button images
    blitBufferToBufferStretchAndFixEdges(
        _preferencesFrmImages[PREFERENCES_WINDOW_FRM_LITTLE_RED_BUTTON_UP].getData(),
        buttonBaseWidth, buttonBaseHeight, buttonBaseWidth,
        scaledNormal, buttonWidth, buttonHeight, buttonWidth);

    blitBufferToBufferStretchAndFixEdges(
        _preferencesFrmImages[PREFERENCES_WINDOW_FRM_LITTLE_RED_BUTTON_DOWN].getData(),
        buttonBaseWidth, buttonBaseHeight, buttonBaseWidth,
        scaledPressed, buttonWidth, buttonHeight, buttonWidth);

    // Scale toggle button images
    blitBufferToBufferStretchAndFixEdges(
        _preferencesFrmImages[PREFERENCES_WINDOW_FRM_CHECKBOX_OFF].getData(),
        toggleBaseWidth, toggleBaseHeight, toggleBaseWidth,
        toggleNormal, toggleWidth, toggleHeight, toggleWidth);

    blitBufferToBufferStretchAndFixEdges(
        _preferencesFrmImages[PREFERENCES_WINDOW_FRM_CHECKBOX_ON].getData(),
        toggleBaseWidth, toggleBaseHeight, toggleBaseWidth,
        togglePressed, toggleWidth, toggleHeight, toggleWidth);

    // PLAYER SPEEDUP toggle button
    _plyrspdbid = buttonCreate(gPreferencesWindow,
        static_cast<int>(383 * scaleX),
        static_cast<int>(68 * scaleY),
        toggleWidth,
        toggleHeight,
        -1,
        -1,
        524,
        524,
        toggleNormal,
        togglePressed,
        nullptr,
        BUTTON_FLAG_TRANSPARENT | BUTTON_FLAG_0x01 | BUTTON_FLAG_0x02);
    if (_plyrspdbid != -1) {
        _win_set_button_rest_state(_plyrspdbid, gPreferencesPlayerSpeedup1, 0);
    }
    buttonSetCallbacks(_plyrspdbid, _gsound_med_butt_press, _gsound_med_butt_press);

    // DEFAULT button
    btn = buttonCreate(gPreferencesWindow,
        static_cast<int>(23 * scaleX),
        static_cast<int>(450 * scaleY),
        buttonWidth,
        buttonHeight,
        -1,
        -1,
        -1,
        527,
        scaledNormal,
        scaledPressed,
        nullptr,
        BUTTON_FLAG_TRANSPARENT);
    if (btn != -1) {
        buttonSetCallbacks(btn, _gsound_red_butt_press, _gsound_red_butt_release);
    }

    // DONE button
    btn = buttonCreate(gPreferencesWindow,
        static_cast<int>(148 * scaleX),
        static_cast<int>(450 * scaleY),
        buttonWidth,
        buttonHeight,
        -1,
        -1,
        -1,
        504,
        scaledNormal,
        scaledPressed,
        nullptr,
        BUTTON_FLAG_TRANSPARENT);
    if (btn != -1) {
        buttonSetCallbacks(btn, _gsound_red_butt_press, _gsound_red_butt_release);
    }

    // CANCEL button
    btn = buttonCreate(gPreferencesWindow,
        static_cast<int>(263 * scaleX),
        static_cast<int>(450 * scaleY),
        buttonWidth,
        buttonHeight,
        -1,
        -1,
        -1,
        528,
        scaledNormal,
        scaledPressed,
        nullptr,
        BUTTON_FLAG_TRANSPARENT);
    if (btn != -1) {
        buttonSetCallbacks(btn, _gsound_red_butt_press, _gsound_red_butt_release);
    }

    // Call update once here to draw all fonts to compositeBuffer before stretching
    for (i = 0; i < PREF_COUNT; i++) {
        _UpdateThing(i);
    }

    // Stretch and blit the text+background into our preference window
    if (scaledWidth != originalWidth || scaledHeight != originalHeight) {
        unsigned char* stretched = reinterpret_cast<unsigned char*>(SDL_malloc(scaledWidth * scaledHeight));
        if (!stretched) {
            SDL_free(compositeBuffer);
            return preferences_fatal_error();
        }

        blitBufferToBufferStretchAndFixEdges(
            compositeBuffer, originalWidth, originalHeight, originalWidth,
            stretched, scaledWidth, scaledHeight, scaledWidth);

        blitBufferToBuffer(stretched, scaledWidth, scaledHeight, scaledWidth, gPreferencesWindowBuffer, scaledWidth);
        SDL_free(stretched);
    } else {
        blitBufferToBuffer(compositeBuffer, originalWidth, originalHeight, originalWidth, gPreferencesWindowBuffer, scaledWidth);
    }

    // Copy the stretched gPreferencesWindowBuffer to the clean buffer (after stretching)
    memcpy(cleanPreferencesBuffer, gPreferencesWindowBuffer, scaledWidth * scaledHeight);

    SDL_free(compositeBuffer);
    _preferencesBackgroundFrmImage.unlock();

    // Call again to draw all buttons
    for (i = 0; i < PREF_COUNT; i++) {
        _UpdateThing(i);
    }

    for (i = 0; i < PREF_COUNT; i++) {
        int mouseEnterEventCode;
        int mouseExitEventCode;
        int mouseDownEventCode;
        int mouseUpEventCode;

        if (i >= FIRST_RANGE_PREF) {
            x = 384;
            y = gPreferenceDescriptions[i].knobY - 12;
            width = 240;
            height = 23;
            mouseEnterEventCode = 526;
            mouseExitEventCode = 526;
            mouseDownEventCode = 505 + i;
            mouseUpEventCode = 526;

        } else if (i >= FIRST_SECONDARY_PREF) {
            x = gPreferenceDescriptions[i].minX;
            y = gPreferenceDescriptions[i].knobY - 5;
            width = gPreferenceDescriptions[i].maxX - x;
            height = 28;
            mouseEnterEventCode = -1;
            mouseExitEventCode = -1;
            mouseDownEventCode = -1;
            mouseUpEventCode = 505 + i;
        } else {
            x = gPreferenceDescriptions[i].minX;
            y = gPreferenceDescriptions[i].knobY - 4;
            width = gPreferenceDescriptions[i].maxX - x;
            height = 48;
            mouseEnterEventCode = -1;
            mouseExitEventCode = -1;
            mouseDownEventCode = -1;
            mouseUpEventCode = 505 + i;
        }

        gPreferenceDescriptions[i].btn = buttonCreate(gPreferencesWindow, static_cast<int>(x * scaleX), static_cast<int>(y * scaleY), static_cast<int>(width * scaleX), static_cast<int>(height * scaleY), mouseEnterEventCode, mouseExitEventCode, mouseDownEventCode, mouseUpEventCode, nullptr, nullptr, nullptr, 32);
    }

    fontSetCurrent(101);

    windowRefresh(gPreferencesWindow);

    return 0;
}

// 0x492870
static int preferencesWindowFree()
{
    if (_changed) {
        _SavePrefs(1);
        _JustUpdate_();
        _combat_highlight_change();
    }

    windowDestroy(gPreferencesWindow);

    for (int index = 0; index < PREFERENCES_WINDOW_FRM_COUNT; index++) {
        _preferencesFrmImages[index].unlock();
    }

    fontSetCurrent(_oldFont);

    messageListFree(&gPreferencesMessageList);

    return 0;
}

// 0x490798
int doPreferences(bool animated)
{
    ScopedGameMode gm(GameMode::kPreferences);

    if (animated) {
        screenWidth = screenGetWidth();
        screenHeight = screenGetHeight();
    } else {
        screenWidth = PREFERENCES_WINDOW_WIDTH;
        screenHeight = PREFERENCES_WINDOW_HEIGHT;
    }

    if (preferencesWindowInit() == -1) {
        debugPrint("\nPREFERENCE MENU: Error loading preference dialog data!\n");
        return -1;
    }

    bool cursorWasHidden = cursorIsHidden();
    if (cursorWasHidden) {
        mouseShowCursor();
    }

    if (animated) {
        colorPaletteLoad("color.pal");
        paletteFadeTo(_cmap);
    }

    int rc = -1;
    while (rc == -1) {
        sharedFpsLimiter.mark();

        int eventCode = inputGetInput();

        switch (eventCode) {
        case KEY_RETURN:
        case KEY_UPPERCASE_P:
        case KEY_LOWERCASE_P:
            soundPlayFile("ib1p1xx1");
            // FALLTHROUGH
        case 504:
            rc = 1;
            break;
        case KEY_CTRL_Q:
        case KEY_CTRL_X:
        case KEY_F10:
            showQuitConfirmationDialog();
            break;
        case KEY_EQUAL:
        case KEY_PLUS:
            brightnessIncrease();
            break;
        case KEY_MINUS:
        case KEY_UNDERSCORE:
            brightnessDecrease();
            break;
        case KEY_F12:
            takeScreenshot();
            break;
        case 527:
            preferencesSetDefaults(true);
            break;
        default:
            if (eventCode == KEY_ESCAPE || eventCode == 528 || _game_user_wants_to_quit != 0) {
                _RestoreSettings();
                rc = 0;
            } else if (eventCode >= 505 && eventCode <= 524) {
                _DoThing(eventCode);
            }
            break;
        }

        renderPresent();
        sharedFpsLimiter.throttle();
    }

    if (animated) {
        paletteFadeTo(gPaletteBlack);
    }

    if (cursorWasHidden) {
        mouseHideCursor();
    }

    preferencesWindowFree();

    return rc;
}

// 0x490E8C
static void _DoThing(int eventCode)
{
    int rawMouseX;
    int rawMouseY;
    mouseGetPositionInWindow(gPreferencesWindow, &rawMouseX, &rawMouseY);

    int mouseX = static_cast<int>(rawMouseX / scaleX);
    int mouseY = static_cast<int>(rawMouseY / scaleY);

    // This preference index also contains out-of-bounds value 19,
    // which is the only preference expressed as checkbox.
    int preferenceIndex = eventCode - 505;

    if (preferenceIndex >= FIRST_PRIMARY_PREF && preferenceIndex <= LAST_PRIMARY_PREF) {
        PreferenceDescription* meta = &(gPreferenceDescriptions[preferenceIndex]);
        int* valuePtr = meta->valuePtr;
        int value = *valuePtr;
        bool valueChanged = false;

        int v1 = meta->knobX + 23;
        int v2 = meta->knobY + 21;
        int knobY = static_cast<int>(meta->knobY * scaleY);

        if (sqrt(pow((double)mouseX - (double)v1, 2) + pow((double)mouseY - (double)v2, 2)) > 16.0) {
            if (mouseY > meta->knobY) {
                int v14 = meta->knobY + word_48FBFE[0];
                if (mouseY >= v14 && mouseY <= v14 + fontGetLineHeight()) {
                    if (mouseX >= meta->minX && mouseX <= meta->knobX) {
                        *valuePtr = 0;
                        meta->direction = 0;
                        valueChanged = true;
                    } else {
                        if (meta->valuesCount >= 3 && mouseX >= meta->knobX + word_48FBF6[2] && mouseX <= meta->maxX) {
                            *valuePtr = 2;
                            meta->direction = 0;
                            valueChanged = true;
                        }
                    }
                }
            } else {
                if (mouseX >= meta->knobX + 9 && mouseX <= meta->knobX + 37) {
                    *valuePtr = 1;
                    if (value != 0) {
                        meta->direction = 1;
                    } else {
                        meta->direction = 0;
                    }
                    valueChanged = true;
                }
            }

            if (meta->valuesCount == 4) {
                int v19 = meta->knobY + word_48FBFE[3];
                if (mouseY >= v19 && mouseY <= v19 + 2 * fontGetLineHeight() && mouseX >= meta->knobX + word_48FBF6[3] && mouseX <= meta->maxX) {
                    *valuePtr = 3;
                    meta->direction = 1;
                    valueChanged = true;
                }
            }
        } else {
            if (meta->direction != 0) {
                if (value == 0) {
                    meta->direction = 0;
                }
            } else {
                if (value == meta->valuesCount - 1) {
                    meta->direction = 1;
                }
            }

            if (meta->direction != 0) {
                *valuePtr = value - 1;
            } else {
                *valuePtr = value + 1;
            }

            valueChanged = true;
        }

        if (valueChanged) {
            soundPlayFile("ib3p1xx1");
            inputBlockForTocks(70);
            soundPlayFile("ib3lu1x1");
            _UpdateThing(preferenceIndex);
            windowRefresh(gPreferencesWindow);
            _changed = true;
            return;
        }
    } else if (preferenceIndex >= FIRST_SECONDARY_PREF && preferenceIndex <= LAST_SECONDARY_PREF) {
        PreferenceDescription* meta = &(gPreferenceDescriptions[preferenceIndex]);
        int* valuePtr = meta->valuePtr;
        int value = *valuePtr;
        bool valueChanged = false;

        int v1 = meta->knobX + 11;
        int v2 = meta->knobY + 12;

        if (sqrt(pow((double)mouseX - (double)v1, 2) + pow((double)mouseY - (double)v2, 2)) > 10.0) {
            int v23 = meta->knobY - 5;
            if (mouseY >= v23 && mouseY <= v23 + fontGetLineHeight() + 2) {
                if (mouseX >= meta->minX && mouseX <= meta->knobX) {
                    *valuePtr = preferenceIndex == PREF_COMBAT_MESSAGES ? 1 : 0;
                    valueChanged = true;
                } else if (mouseX >= meta->knobX + 22.0 && mouseX <= meta->maxX) {
                    *valuePtr = preferenceIndex == PREF_COMBAT_MESSAGES ? 0 : 1;
                    valueChanged = true;
                }
            }
        } else {
            *valuePtr ^= 1;
            valueChanged = true;
        }

        if (valueChanged) {
            soundPlayFile("ib2p1xx1");
            inputBlockForTocks(70);
            soundPlayFile("ib2lu1x1");
            _UpdateThing(preferenceIndex);
            windowRefresh(gPreferencesWindow);
            _changed = true;
            return;
        }
    } else if (preferenceIndex >= FIRST_RANGE_PREF && preferenceIndex <= LAST_RANGE_PREF) {
        PreferenceDescription* meta = &(gPreferenceDescriptions[preferenceIndex]);
        int* valuePtr = meta->valuePtr;

        soundPlayFile("ib1p1xx1");

        double value;
        switch (preferenceIndex) {
        case PREF_TEXT_BASE_DELAY:
            value = 6.0 - gPreferencesTextBaseDelay1 + 1.0;
            break;
        case PREF_BRIGHTNESS:
            value = gPreferencesBrightness1;
            break;
        case PREF_MOUSE_SENSITIVIY:
            value = gPreferencesMouseSensitivity1;
            break;
        default:
            value = *valuePtr;
            break;
        }

        int knobX = (int)(219.0 / (meta->maxValue - meta->minValue));
        int v31 = (int)((value - meta->minValue) * (219.0 / (meta->maxValue - meta->minValue)) + 384.0);

        int off = scaledWidth * static_cast<int>((meta->knobY * scaleY) - (3 * scaleY)) + (384 * scaleX);
        blitBufferToBuffer(cleanPreferencesBuffer + off, (240 * scaleX), (24 * scaleX), scaledWidth, gPreferencesWindowBuffer + off, scaledWidth);

        blitBufferToBufferTrans(gKnobImageStretched, 21 * scaleX, 11 * scaleY, 21 * scaleX, gPreferencesWindowBuffer + scaledWidth * static_cast<int>(meta->knobY * scaleY + static_cast<int>(1 * scaleY)) + static_cast<int>(v31 * scaleX), scaledWidth);

        windowRefresh(gPreferencesWindow);

        int sfxVolumeExample = 0;
        int speechVolumeExample = 0;
        while (true) {
            sharedFpsLimiter.mark();

            inputGetInput();

            int tick = getTicks();

            mouseGetPositionInWindow(gPreferencesWindow, &rawMouseX, &rawMouseY);

            mouseX = static_cast<int>(rawMouseX / scaleX);
            mouseY = static_cast<int>(rawMouseY / scaleY);

            if (mouseGetEvent() & 0x10) {
                soundPlayFile("ib1lu1x1");
                _UpdateThing(preferenceIndex);
                windowRefresh(gPreferencesWindow);
                renderPresent();
                _changed = true;
                return;
            }

            if (v31 + 14 > mouseX) {
                if (v31 + 6 > mouseX) {
                    v31 = mouseX - 6;
                    if (v31 < 384) {
                        v31 = 384;
                    }
                }
            } else {
                v31 = mouseX - 6;
                if (v31 > 603) {
                    v31 = 603;
                }
            }

            double newValue = ((double)v31 - 384.0) / (219.0 / (meta->maxValue - meta->minValue)) + meta->minValue;

            int v52 = 0;

            switch (preferenceIndex) {
            case PREF_COMBAT_SPEED:
                *meta->valuePtr = (int)newValue;
                break;
            case PREF_TEXT_BASE_DELAY:
                gPreferencesTextBaseDelay1 = 6.0 - newValue + 1.0;
                break;
            case PREF_MASTER_VOLUME:
                *meta->valuePtr = (int)newValue;
                gameSoundSetMasterVolume(gPreferencesMasterVolume1);
                v52 = 1;
                break;
            case PREF_MUSIC_VOLUME:
                *meta->valuePtr = (int)newValue;
                backgroundSoundSetVolume(gPreferencesMusicVolume1);
                v52 = 1;
                break;
            case PREF_SFX_VOLUME:
                *meta->valuePtr = (int)newValue;
                soundEffectsSetVolume(gPreferencesSoundEffectsVolume1);
                v52 = 1;
                if (sfxVolumeExample == 0) {
                    soundPlayFile("butin1");
                    sfxVolumeExample = 7;
                } else {
                    sfxVolumeExample--;
                }
                break;
            case PREF_SPEECH_VOLUME:
                *meta->valuePtr = (int)newValue;
                speechSetVolume(gPreferencesSpeechVolume1);
                v52 = 1;
                if (speechVolumeExample == 0) {
                    speechLoad("narrator\\options", 12, 13, 15);
                    speechVolumeExample = 40;
                } else {
                    speechVolumeExample--;
                }
                break;
            case PREF_BRIGHTNESS:
                gPreferencesBrightness1 = newValue;
                colorSetBrightness(newValue);
                break;
            case PREF_MOUSE_SENSITIVIY:
                gPreferencesMouseSensitivity1 = newValue;
                break;
            }

            if (v52) {
                int off = scaledWidth * static_cast<int>((meta->knobY * scaleY) - (3 * scaleY)) + (384 * scaleX);
                blitBufferToBuffer(cleanPreferencesBuffer + off, (240 * scaleX), (24 * scaleX), scaledWidth, gPreferencesWindowBuffer + off, scaledWidth);

                for (int optionIndex = 0; optionIndex < meta->valuesCount; optionIndex++) {
                    const char* str = getmsg(&gPreferencesMessageList, &gPreferencesMessageListItem, meta->labelIds[optionIndex]);

                    int x;
                    switch (optionIndex) {
                    case 0:
                        // 0x4926AA
                        x = 384;
                        // TODO: Incomplete.
                        break;
                    case 1:
                        // 0x4926F3
                        switch (meta->valuesCount) {
                        case 2:
                            x = 624 - fontGetStringWidth(str);
                            break;
                        case 3:
                            // This code path does not use floating-point arithmetic
                            x = 504 - fontGetStringWidth(str) / 2 - 2;
                            break;
                        case 4:
                            // Uses floating-point arithmetic
                            x = 444 + fontGetStringWidth(str) / 2 - 8;
                            break;
                        }
                        break;
                    case 2:
                        // 0x492766
                        switch (meta->valuesCount) {
                        case 3:
                            x = 624 - fontGetStringWidth(str);
                            break;
                        case 4:
                            // Uses floating-point arithmetic
                            x = 564 - fontGetStringWidth(str) - 4;
                            break;
                        }
                        break;
                    case 3:
                        // 0x49279E
                        x = 624 - fontGetStringWidth(str);
                        break;
                    }
                    fontDrawText(compositeBuffer + PREFERENCES_WINDOW_WIDTH * (meta->knobY - 12) + x, str, PREFERENCES_WINDOW_WIDTH, PREFERENCES_WINDOW_WIDTH, _colorTable[18979]);
                }
            } else {
                int off = scaledWidth * static_cast<int>((meta->knobY * scaleY) - (3 * scaleY)) + (384 * scaleX);
                blitBufferToBuffer(cleanPreferencesBuffer + off, (240 * scaleX), (24 * scaleX), scaledWidth, gPreferencesWindowBuffer + off, scaledWidth);
            }

            blitBufferToBufferTrans(gKnobImageStretched, 21 * scaleX, 11 * scaleY, 21 * scaleX, gPreferencesWindowBuffer + scaledWidth * static_cast<int>(meta->knobY * scaleY + static_cast<int>(1 * scaleY)) + static_cast<int>(v31 * scaleX), scaledWidth);
            windowRefresh(gPreferencesWindow);

            delay_ms(35 - (getTicks() - tick));

            renderPresent();
            sharedFpsLimiter.throttle();
        }
    } else if (preferenceIndex == 19) {
        gPreferencesPlayerSpeedup1 ^= 1;
    }

    _changed = true;
}

static int preferences_fatal_error()
{
    preferencesWindowFree();

    return -1;
}

}
