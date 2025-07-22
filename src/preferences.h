#ifndef FALLOUT_PREFERENCES_H_
#define FALLOUT_PREFERENCES_H_

#include "db.h"
#include "geometry.h"

namespace fallout {

// Preference IDs
typedef enum Preference {
    // Primary preferences (5)
    PREF_GAME_DIFFICULTY,
    PREF_COMBAT_DIFFICULTY,
    PREF_VIOLENCE_LEVEL,
    PREF_TARGET_HIGHLIGHT,
    PREF_COMBAT_LOOKS,

    // Secondary preferences (6)
    PREF_COMBAT_MESSAGES,
    PREF_COMBAT_TAUNTS,
    PREF_LANGUAGE_FILTER,
    PREF_RUNNING,
    PREF_SUBTITLES,
    PREF_ITEM_HIGHLIGHT,

    // Range preferences (8)
    PREF_COMBAT_SPEED,
    PREF_TEXT_BASE_DELAY,
    PREF_MASTER_VOLUME,
    PREF_MUSIC_VOLUME,
    PREF_SFX_VOLUME,
    PREF_SPEECH_VOLUME,
    PREF_BRIGHTNESS,
    PREF_MOUSE_SENSITIVIY,

    // Special cases
    PREF_PLAYER_SPEEDUP = 19, // The checkbox preference

    // Total count (don't include in count)
    PREF_COUNT
} Preference;

// Constants for preference categories
enum {
    PRIMARY_PREF_COUNT = 5,
    SECONDARY_PREF_COUNT = 6,
    RANGE_PREF_COUNT = 8,

    FIRST_PRIMARY_PREF = PREF_GAME_DIFFICULTY,
    LAST_PRIMARY_PREF = PREF_COMBAT_LOOKS,

    FIRST_SECONDARY_PREF = PREF_COMBAT_MESSAGES,
    LAST_SECONDARY_PREF = PREF_ITEM_HIGHLIGHT,

    FIRST_RANGE_PREF = PREF_COMBAT_SPEED,
    LAST_RANGE_PREF = PREF_MOUSE_SENSITIVIY,

    PRIMARY_OPTION_VALUE_COUNT = 4,
    SECONDARY_OPTION_VALUE_COUNT = 2
};

// FRM IDs
typedef enum PreferencesWindowFrm {
    PREFERENCES_WINDOW_FRM_BACKGROUND,
    PREFERENCES_WINDOW_FRM_KNOB_OFF,
    PREFERENCES_WINDOW_FRM_PRIMARY_SWITCH,
    PREFERENCES_WINDOW_FRM_SECONDARY_SWITCH,
    PREFERENCES_WINDOW_FRM_CHECKBOX_ON,
    PREFERENCES_WINDOW_FRM_CHECKBOX_OFF,
    PREFERENCES_WINDOW_FRM_6,
    PREFERENCES_WINDOW_FRM_KNOB_ON,
    PREFERENCES_WINDOW_FRM_LITTLE_RED_BUTTON_UP,
    PREFERENCES_WINDOW_FRM_LITTLE_RED_BUTTON_DOWN,
    PREFERENCES_WINDOW_FRM_COUNT
} PreferencesWindowFrm;

// Preference description structure
typedef struct PreferenceDescription {
    short valuesCount;
    short direction;
    short knobX;
    short knobY;
    short minX;
    short maxX;
    short labelIds[PRIMARY_OPTION_VALUE_COUNT];
    int btn;
    char name[32];
    double minValue;
    double maxValue;
    int* valuePtr;
} PreferenceDescription;

int preferencesInit();
int doPreferences(bool animated);
int preferencesSave(File* stream);
int preferencesLoad(File* stream);
void brightnessIncrease();
void brightnessDecrease();

// Global declarations
extern const int _row1Ytab[PRIMARY_PREF_COUNT];
extern const int _row2Ytab[SECONDARY_PREF_COUNT];
extern const int _row3Ytab[RANGE_PREF_COUNT];
extern const short word_48FBF6[PRIMARY_OPTION_VALUE_COUNT];
extern const short word_48FBFE[PRIMARY_OPTION_VALUE_COUNT];
extern const short word_48FC06[SECONDARY_OPTION_VALUE_COUNT];
extern const int dword_48FC1C[PRIMARY_PREF_COUNT];
extern const int dword_48FC30[SECONDARY_PREF_COUNT];

} // namespace fallout

#endif /* FALLOUT_PREFERENCES_H_ */
