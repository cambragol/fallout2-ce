#include "art.h"

#include <stdio.h>

#include "db.h" // for fileOpen, fileClose
#include "xfile.h" // for File type
#define DIR_SEPARATOR '/'

#include <stdlib.h>
#include <string.h>

#include "animation.h"
#include "debug.h"
#include "draw.h"
#include "game.h"
#include "memory.h"
#include "object.h"
#include "proto.h"
#include "settings.h"
#include "sfall_config.h"
#include "window_manager.h"


namespace fallout {

typedef struct ArtListDescription {
    int flags;
    char name[16];
    void* field_18;
    char* fileNames;          // Combined: vanilla + variants + mods
    int fileNamesLength;      // Total entries
    
    // Add these three counters
    int vanillaCount;         // Original vanilla entries
    int variantCount;         // Added variants
    int modCount;             // Added mod entries
} ArtListDescription;

typedef struct HeadDescription {
    int goodFidgetCount;
    int neutralFidgetCount;
    int badFidgetCount;
} HeadDescription;

static int artReadList(const char* path, char** out_arr, int* out_count);
static int artCacheGetFileSizeImpl(int fid, int* out_size);
static int artCacheReadDataImpl(int fid, int* sizePtr, unsigned char* data);
static void artCacheFreeImpl(void* ptr);
static int artReadFrameData(unsigned char* data, File* stream, int count, int* paddingPtr);
static int artReadHeader(Art* art, File* stream);
static int artGetDataSize(Art* art);
static int paddingForSize(int size);

// 0x5002D8
static char gDefaultJumpsuitMaleFileName[] = "hmjmps";

// 0x05002E0
static char gDefaultJumpsuitFemaleFileName[] = "hfjmps";

// 0x5002E8
static char gDefaultTribalMaleFileName[] = "hmwarr";

// 0x5002F0
static char gDefaultTribalFemaleFileName[] = "hfprim";

// 0x510738
static ArtListDescription gArtListDescriptions[OBJ_TYPE_COUNT] = {
    { 0, "items", nullptr, nullptr, 0 },
    { 0, "critters", nullptr, nullptr, 0 },
    { 0, "scenery", nullptr, nullptr, 0 },
    { 0, "walls", nullptr, nullptr, 0 },
    { 0, "tiles", nullptr, nullptr, 0 },
    { 0, "misc", nullptr, nullptr, 0 },
    { 0, "intrface", nullptr, nullptr, 0 },
    { 0, "inven", nullptr, nullptr, 0 },
    { 0, "heads", nullptr, nullptr, 0 },
    { 0, "backgrnd", nullptr, nullptr, 0 },
    { 0, "skilldex", nullptr, nullptr, 0 },
};

// This flag denotes that localized arts should be looked up first. Used
// together with [gArtLanguage].
//
// 0x510898
static bool gArtLanguageInitialized = false;

// 0x51089C
static const char* _head1 = "gggnnnbbbgnb";

// 0x5108A0
static const char* _head2 = "vfngfbnfvppp";

// Current native look base fid.
//
// 0x5108A4
int _art_vault_guy_num = 0;

// Base fids for unarmored dude.
//
// Outfit file names:
// - tribal: "hmwarr", "hfprim"
// - jumpsuit: "hmjmps", "hfjmps"
//
// NOTE: This value could have been done with two separate arrays - one for
// tribal look, and one for jumpsuit look. However in this case it would have
// been accessed differently in 0x49F984, which clearly uses look type as an
// index, not gender.
//
// 0x5108A8
int _art_vault_person_nums[DUDE_NATIVE_LOOK_COUNT][GENDER_COUNT];

// Index of "grid001.frm" in tiles.lst.
//
// 0x5108B8
static int _art_mapper_blank_tile = 1;

// Non-english language name.
//
// This value is used as a directory name to display localized arts.
//
// 0x56C970
static char gArtLanguage[32];

// 0x56C990
Cache gArtCache;

// 0x56C9E4
static char _art_name[COMPAT_MAX_PATH];

// head_info
// 0x56CAE8
static HeadDescription* gHeadDescriptions;

// anon_alias
// 0x56CAEC
static int* _anon_alias;

// artCritterFidShouldRunData
// 0x56CAF0
static int* gArtCritterFidShoudRunData;

// Number of “core” entries per objectType, before we append variants
static int gArtOriginalCount[OBJ_TYPE_COUNT] = { 0 };

int artGetFidWithVariant(int objectType, int baseId, const char* suffix, bool useVariant)
{
    if (useVariant) {
        int variantId = artFindVariant(objectType, baseId, suffix);
        if (variantId >= 0) {
            return buildFid(objectType, variantId, 0, 0, 0);
        }
    }
    return buildFid(objectType, baseId, 0, 0, 0);
}

int artFindVariant(int objectType, int baseIndex, const char* filename) {
    if (objectType < 0 || objectType >= OBJ_TYPE_COUNT)
        return -1;

    ArtListDescription* desc = &gArtListDescriptions[objectType];
    
    // NEW: Filename search mode for mod assets
    if (baseIndex == -1) {
        for (int i = 0; i < desc->fileNamesLength; i++) {
            char* candidate = desc->fileNames + (i * FILENAME_LENGTH);
            if (compat_stricmp(candidate, filename) == 0) {
                return i;
            }
        }
        return -1;
    }
    
    // EXISTING VARIANT LOGIC (unchanged)
    if (baseIndex < 0 || baseIndex >= desc->fileNamesLength)
        return -1;

    // Get base filename
    const char* baseName = desc->fileNames + baseIndex * FILENAME_LENGTH;

    // Extract base without extension
    char base[FILENAME_LENGTH];
    strncpy(base, baseName, FILENAME_LENGTH - 1);
    base[FILENAME_LENGTH - 1] = '\0';

    char* ext = strrchr(base, '.');
    if (ext && compat_stricmp(ext, ".frm") == 0) {
        *ext = '\0'; // Remove extension
    }

    // Build expected variant name
    char expected[FILENAME_LENGTH];
    int len = snprintf(expected, sizeof(expected), "%s%s.frm", base, filename);
    if (len >= FILENAME_LENGTH) {
        debugPrint("Variant name too long: %s%s", base, filename);
        return -1;
    }

    // Search variant section
    for (int i = desc->vanillaCount; i < desc->vanillaCount + desc->variantCount; i++) {
        const char* candidate = desc->fileNames + i * FILENAME_LENGTH;
        if (compat_stricmp(candidate, expected) == 0) {
            return i;
        }
    }

    return -1;
}

// 0x418840
int artInit()
{
    char path[COMPAT_MAX_PATH];
    File* stream;
    char string[200];

    int cacheSize = settings.system.art_cache_size;
    if (!cacheInit(&gArtCache, artCacheGetFileSizeImpl, artCacheReadDataImpl, artCacheFreeImpl, cacheSize << 20)) {
        debugPrint("cache_init failed in art_init\n");
        return -1;
    }

    const char* language = settings.system.language.c_str();
    if (compat_stricmp(language, ENGLISH) != 0) {
        strcpy(gArtLanguage, language);
        gArtLanguageInitialized = true;
    }

    bool critterDbSelected = false;
    for (int objectType = 0; objectType < OBJ_TYPE_COUNT; objectType++) {
        ArtListDescription* desc = &gArtListDescriptions[objectType];
        desc->flags = 0;
        snprintf(path, sizeof(path), "%s%s%s\\%s.lst", _cd_path_base, "art\\", gArtListDescriptions[objectType].name, gArtListDescriptions[objectType].name);

        // 1. Load VANILLA assets
        if (artReadList(path, &(desc->fileNames), &(desc->fileNamesLength)) != 0) {
            debugPrint("art_read_lst failed in art_init\n");
            cacheFree(&gArtCache);
            return -1;
        }
        desc->vanillaCount = desc->fileNamesLength;  // Store vanilla count

        // 2. Process VARIANTS (original code)
        const char* suffix = "_800.frm";
        size_t suffixLen = strlen(suffix);

        char pattern[COMPAT_MAX_PATH];
        snprintf(pattern, sizeof(pattern),
            "art%c%s%c*.frm",
            DIR_SEPARATOR,
            desc->name,
            DIR_SEPARATOR);

        char** foundFiles = NULL;
        int fileCount = fileNameListInit(pattern, &foundFiles, 0, 0);

        if (fileCount > 0) {
            int origCount = desc->fileNamesLength;
            int newCount = origCount;
            char* names = desc->fileNames;
            int currentSize = desc->fileNamesLength;

            for (int i = 0; i < fileCount; i++) {
                const char* filename = foundFiles[i];
                size_t len = strlen(filename);

                if (len <= suffixLen || compat_stricmp(filename + len - suffixLen, suffix) != 0) {
                    continue;
                }

                const char* baseName = strrchr(filename, DIR_SEPARATOR);
                if (!baseName)
                    baseName = filename;
                else
                    baseName++;

                char variantBase[FILENAME_LENGTH] = { 0 };
                size_t baseLen = strlen(baseName) - suffixLen;
                if (baseLen >= FILENAME_LENGTH)
                    baseLen = FILENAME_LENGTH - 1;
                strncpy(variantBase, baseName, baseLen);
                variantBase[baseLen] = '\0';

                bool matchFound = false;
                for (int j = 0; j < origCount; j++) {
                    const char* slot = names + j * FILENAME_LENGTH;

                    const char* slotName = strrchr(slot, DIR_SEPARATOR);
                    if (!slotName)
                        slotName = slot;
                    else
                        slotName++;

                    char slotBase[FILENAME_LENGTH];
                    strncpy(slotBase, slotName, FILENAME_LENGTH);
                    char* ext = strrchr(slotBase, '.');
                    if (ext)
                        *ext = '\0';

                    if (compat_stricmp(slotBase, variantBase) == 0) {
                        matchFound = true;
                        break;
                    }
                }

                if (!matchFound)
                    continue;

                if (newCount >= currentSize) {
                    currentSize += 10;
                    char* newNames = (char*)internal_realloc(names, currentSize * FILENAME_LENGTH);
                    if (!newNames)
                        break;
                    names = newNames;
                }

                char* dest = names + newCount * FILENAME_LENGTH;
                strncpy(dest, baseName, FILENAME_LENGTH - 1);
                dest[FILENAME_LENGTH - 1] = '\0';
                newCount++;
            }

            if (newCount > origCount) {
                desc->fileNames = names;
                desc->fileNamesLength = newCount;
            }

            fileNameListFree(&foundFiles, fileCount);
        }
        desc->variantCount = desc->fileNamesLength - desc->vanillaCount;  // Store variant count
        
        // 3. Load MODS (NEW CODE)
// In the mod loading section of artInit()
char searchPattern[COMPAT_MAX_PATH];
snprintf(searchPattern, sizeof(searchPattern), 
         "%sart%c%s%cmod_*.lst",  // Corrected pattern
         _cd_path_base,            // Base game directory
         DIR_SEPARATOR,            // Directory separator (/ or \)
         desc->name,               // Object type name (e.g., "intrface")
         DIR_SEPARATOR);

char** foundModFiles = nullptr;
int modFileCount = fileNameListInit(searchPattern, &foundModFiles, 0, 0);
        desc->modCount = 0;
        
if (modFileCount > 0) {
    // Build base directory path for mod files
    char baseDir[COMPAT_MAX_PATH];
    snprintf(baseDir, sizeof(baseDir), "%sart%c%s%c",
             _cd_path_base,
             DIR_SEPARATOR,
             desc->name,
             DIR_SEPARATOR);
    
    for (int i = 0; i < modFileCount; i++) {
        // Extract the base filename (e.g., "mod_test1.lst")
        const char* baseName = foundModFiles[i];
        
        // Construct the full path MANUALLY
        char fullPath[COMPAT_MAX_PATH];
        snprintf(fullPath, sizeof(fullPath), "%s%s", baseDir, baseName);
        
        char* modEntries = nullptr;
        int modEntryCount = 0;
        
        // Use our manually constructed full path
        debugPrint("Loading mod LST: %s", fullPath);
        if (artReadList(fullPath, &modEntries, &modEntryCount) == 0) {
            // Append mod entries to main list
            int newLength = desc->fileNamesLength + modEntryCount;
            char* newNames = (char*)internal_realloc(desc->fileNames, newLength * FILENAME_LENGTH);
            
            if (newNames) {
                // Copy existing entries
                memcpy(newNames, desc->fileNames, desc->fileNamesLength * FILENAME_LENGTH);
                // Append mod entries
                memcpy(newNames + (desc->fileNamesLength * FILENAME_LENGTH), 
                       modEntries, 
                       modEntryCount * FILENAME_LENGTH);
                
                desc->fileNames = newNames;
                desc->fileNamesLength = newLength;
                desc->modCount += modEntryCount;
                
                debugPrint("  Added %d entries from mod", modEntryCount);
            }
            
            internal_free(modEntries);
        } else {
            debugPrint("  Failed to read mod file: %s", fullPath);
        }
    }
    fileNameListFree(&foundModFiles, modFileCount);
}

    }

    _anon_alias = (int*)internal_malloc(sizeof(*_anon_alias) * gArtListDescriptions[OBJ_TYPE_CRITTER].fileNamesLength);
    if (_anon_alias == nullptr) {
        gArtListDescriptions[OBJ_TYPE_CRITTER].fileNamesLength = 0;
        debugPrint("Out of memory for anon_alias in art_init\n");
        cacheFree(&gArtCache);
        return -1;
    }

    gArtCritterFidShoudRunData = (int*)internal_malloc(sizeof(*gArtCritterFidShoudRunData) * gArtListDescriptions[1].fileNamesLength);
    if (gArtCritterFidShoudRunData == nullptr) {
        gArtListDescriptions[OBJ_TYPE_CRITTER].fileNamesLength = 0;
        debugPrint("Out of memory for artCritterFidShouldRunData in art_init\n");
        cacheFree(&gArtCache);
        return -1;
    }

    for (int critterIndex = 0; critterIndex < gArtListDescriptions[OBJ_TYPE_CRITTER].fileNamesLength; critterIndex++) {
        gArtCritterFidShoudRunData[critterIndex] = 0;
    }

    snprintf(path, sizeof(path), "%s%s%s\\%s.lst", _cd_path_base, "art\\", gArtListDescriptions[OBJ_TYPE_CRITTER].name, gArtListDescriptions[OBJ_TYPE_CRITTER].name);

    stream = fileOpen(path, "rt");
    if (stream == nullptr) {
        debugPrint("Unable to open %s in art_init\n", path);
        cacheFree(&gArtCache);
        return -1;
    }

    // SFALL: Modify player model settings.
    char* jumpsuitMaleFileName = nullptr;
    configGetString(&gSfallConfig, SFALL_CONFIG_MISC_KEY, SFALL_CONFIG_DUDE_NATIVE_LOOK_JUMPSUIT_MALE_KEY, &jumpsuitMaleFileName);
    if (jumpsuitMaleFileName == nullptr || jumpsuitMaleFileName[0] == '\0') {
        jumpsuitMaleFileName = gDefaultJumpsuitMaleFileName;
    }

    char* jumpsuitFemaleFileName = nullptr;
    configGetString(&gSfallConfig, SFALL_CONFIG_MISC_KEY, SFALL_CONFIG_DUDE_NATIVE_LOOK_JUMPSUIT_FEMALE_KEY, &jumpsuitFemaleFileName);
    if (jumpsuitFemaleFileName == nullptr || jumpsuitFemaleFileName[0] == '\0') {
        jumpsuitFemaleFileName = gDefaultJumpsuitFemaleFileName;
    }

    char* tribalMaleFileName = nullptr;
    configGetString(&gSfallConfig, SFALL_CONFIG_MISC_KEY, SFALL_CONFIG_DUDE_NATIVE_LOOK_TRIBAL_MALE_KEY, &tribalMaleFileName);
    if (tribalMaleFileName == nullptr || tribalMaleFileName[0] == '\0') {
        tribalMaleFileName = gDefaultTribalMaleFileName;
    }

    char* tribalFemaleFileName = nullptr;
    configGetString(&gSfallConfig, SFALL_CONFIG_MISC_KEY, SFALL_CONFIG_DUDE_NATIVE_LOOK_TRIBAL_FEMALE_KEY, &tribalFemaleFileName);
    if (tribalFemaleFileName == nullptr || tribalFemaleFileName[0] == '\0') {
        tribalFemaleFileName = gDefaultTribalFemaleFileName;
    }

    char* critterFileNames = gArtListDescriptions[OBJ_TYPE_CRITTER].fileNames;
    for (int critterIndex = 0; critterIndex < gArtListDescriptions[OBJ_TYPE_CRITTER].fileNamesLength; critterIndex++) {
        if (compat_stricmp(critterFileNames, jumpsuitMaleFileName) == 0) {
            _art_vault_person_nums[DUDE_NATIVE_LOOK_JUMPSUIT][GENDER_MALE] = critterIndex;
        } else if (compat_stricmp(critterFileNames, jumpsuitFemaleFileName) == 0) {
            _art_vault_person_nums[DUDE_NATIVE_LOOK_JUMPSUIT][GENDER_FEMALE] = critterIndex;
        }

        if (compat_stricmp(critterFileNames, tribalMaleFileName) == 0) {
            _art_vault_person_nums[DUDE_NATIVE_LOOK_TRIBAL][GENDER_MALE] = critterIndex;
            _art_vault_guy_num = critterIndex;
        } else if (compat_stricmp(critterFileNames, tribalFemaleFileName) == 0) {
            _art_vault_person_nums[DUDE_NATIVE_LOOK_TRIBAL][GENDER_FEMALE] = critterIndex;
        }

        critterFileNames += FILENAME_LENGTH;
    }

    for (int critterIndex = 0; critterIndex < gArtListDescriptions[OBJ_TYPE_CRITTER].fileNamesLength; critterIndex++) {
        if (!fileReadString(string, sizeof(string), stream)) {
            break;
        }

        char* sep1 = strchr(string, ',');
        if (sep1 != nullptr) {
            _anon_alias[critterIndex] = atoi(sep1 + 1);

            char* sep2 = strchr(sep1 + 1, ',');
            if (sep2 != nullptr) {
                gArtCritterFidShoudRunData[critterIndex] = atoi(sep2 + 1);
            } else {
                gArtCritterFidShoudRunData[critterIndex] = 0;
            }
        } else {
            _anon_alias[critterIndex] = _art_vault_guy_num;
            gArtCritterFidShoudRunData[critterIndex] = 1;
        }
    }

    fileClose(stream);

    char* tileFileNames = gArtListDescriptions[OBJ_TYPE_TILE].fileNames;
    for (int tileIndex = 0; tileIndex < gArtListDescriptions[OBJ_TYPE_TILE].fileNamesLength; tileIndex++) {
        if (compat_stricmp(tileFileNames, "grid001.frm") == 0) {
            _art_mapper_blank_tile = tileIndex;
        }
        tileFileNames += FILENAME_LENGTH;
    }

    gHeadDescriptions = (HeadDescription*)internal_malloc(sizeof(*gHeadDescriptions) * gArtListDescriptions[OBJ_TYPE_HEAD].fileNamesLength);
    if (gHeadDescriptions == nullptr) {
        gArtListDescriptions[OBJ_TYPE_HEAD].fileNamesLength = 0;
        debugPrint("Out of memory for head_info in art_init\n");
        cacheFree(&gArtCache);
        return -1;
    }

    snprintf(path, sizeof(path), "%s%s%s\\%s.lst", _cd_path_base, "art\\", gArtListDescriptions[OBJ_TYPE_HEAD].name, gArtListDescriptions[OBJ_TYPE_HEAD].name);

    stream = fileOpen(path, "rt");
    if (stream == nullptr) {
        debugPrint("Unable to open %s in art_init\n", path);
        cacheFree(&gArtCache);
        return -1;
    }

    for (int headIndex = 0; headIndex < gArtListDescriptions[OBJ_TYPE_HEAD].fileNamesLength; headIndex++) {
        if (!fileReadString(string, sizeof(string), stream)) {
            break;
        }

        char* sep1 = strchr(string, ',');
        if (sep1 != nullptr) {
            *sep1 = '\0';
        } else {
            sep1 = string;
        }

        char* sep2 = strchr(sep1, ',');
        if (sep2 != nullptr) {
            *sep2 = '\0';
        } else {
            sep2 = sep1;
        }

        gHeadDescriptions[headIndex].goodFidgetCount = atoi(sep1 + 1);

        char* sep3 = strchr(sep2, ',');
        if (sep3 != nullptr) {
            *sep3 = '\0';
        } else {
            sep3 = sep2;
        }

        gHeadDescriptions[headIndex].neutralFidgetCount = atoi(sep2 + 1);

        char* sep4 = strpbrk(sep3 + 1, " ,;\t\n");
        if (sep4 != nullptr) {
            *sep4 = '\0';
        }

        gHeadDescriptions[headIndex].badFidgetCount = atoi(sep3 + 1);
    }

    fileClose(stream);

    File* artListFile = fileOpen("art_list.txt", "wt");
    if (artListFile) {
        for (int objectType = 0; objectType < OBJ_TYPE_COUNT; objectType++) {
            ArtListDescription* desc = &gArtListDescriptions[objectType];
            
            // Write category header
            char header[256];
            snprintf(header, sizeof(header), "[%s] (%d assets)\n", desc->name, desc->fileNamesLength);
            fileWrite(header, strlen(header), 1, artListFile);
            
            // Write entries with slot numbers
            char* names = desc->fileNames;
            for (int i = 0; i < desc->fileNamesLength; i++) {
                char* current = names + (i * FILENAME_LENGTH);
                
                // Skip empty slots
                if (current[0] == '\0') continue;
                
                char line[256];
                snprintf(line, sizeof(line), "%5d = %s\n", i, current);
                fileWrite(line, strlen(line), 1, artListFile);
            }
            
            // Add category separator
            fileWrite("\n", 1, 1, artListFile);
        }
        fileClose(artListFile);
    }

    return 0;
}

// 0x418EB8
void artReset()
{
}

// 0x418EBC
void artExit()
{
    cacheFree(&gArtCache);

    internal_free(_anon_alias);
    internal_free(gArtCritterFidShoudRunData);

    for (int index = 0; index < OBJ_TYPE_COUNT; index++) {
        internal_free(gArtListDescriptions[index].fileNames);
        gArtListDescriptions[index].fileNames = nullptr;

        internal_free(gArtListDescriptions[index].field_18);
        gArtListDescriptions[index].field_18 = nullptr;
    }

    internal_free(gHeadDescriptions);
}

// 0x418F1C
char* artGetObjectTypeName(int objectType)
{
    return objectType >= OBJ_TYPE_ITEM && objectType < OBJ_TYPE_COUNT ? gArtListDescriptions[objectType].name : nullptr;
}

// 0x418F34
int artIsObjectTypeHidden(int objectType)
{
    return objectType >= OBJ_TYPE_ITEM && objectType < OBJ_TYPE_COUNT ? gArtListDescriptions[objectType].flags & 1 : 0;
}

// 0x418F7C
int artGetFidgetCount(int headFid)
{
    if (FID_TYPE(headFid) != OBJ_TYPE_HEAD) {
        return 0;
    }

    int head = headFid & 0xFFF;

    if (head > gArtListDescriptions[OBJ_TYPE_HEAD].fileNamesLength) {
        return 0;
    }

    HeadDescription* headDescription = &(gHeadDescriptions[head]);

    int fidget = (headFid & 0xFF0000) >> 16;
    switch (fidget) {
    case FIDGET_GOOD:
        return headDescription->goodFidgetCount;
    case FIDGET_NEUTRAL:
        return headDescription->neutralFidgetCount;
    case FIDGET_BAD:
        return headDescription->badFidgetCount;
    }
    return 0;
}

// 0x418FFC
void artRender(int fid, unsigned char* dest, int width, int height, int pitch)
{
    // NOTE: Original code is different. For unknown reason it directly calls
    // many art functions, for example instead of [artLock] it calls lower level
    // [cacheLock], instead of [artGetWidth] is calls [artGetFrame], then get
    // width from frame's struct field. I don't know if this was intentional or
    // not. I've replaced these calls with higher level functions where
    // appropriate.

    CacheEntry* handle;
    Art* frm = artLock(fid, &handle);
    if (frm == nullptr) {
        return;
    }

    unsigned char* frameData = artGetFrameData(frm, 0, 0);
    int frameWidth = artGetWidth(frm, 0, 0);
    int frameHeight = artGetHeight(frm, 0, 0);

    int remainingWidth = width - frameWidth;
    int remainingHeight = height - frameHeight;
    if (remainingWidth < 0 || remainingHeight < 0) {
        if (height * frameWidth >= width * frameHeight) {
            blitBufferToBufferStretchTrans(frameData,
                frameWidth,
                frameHeight,
                frameWidth,
                dest + pitch * ((height - width * frameHeight / frameWidth) / 2),
                width,
                width * frameHeight / frameWidth,
                pitch);
        } else {
            blitBufferToBufferStretchTrans(frameData,
                frameWidth,
                frameHeight,
                frameWidth,
                dest + (width - height * frameWidth / frameHeight) / 2,
                height * frameWidth / frameHeight,
                height,
                pitch);
        }
    } else {
        blitBufferToBufferTrans(frameData,
            frameWidth,
            frameHeight,
            frameWidth,
            dest + pitch * (remainingHeight / 2) + remainingWidth / 2,
            pitch);
    }

    artUnlock(handle);
}

// mapper2.exe: 0x40A03C
int art_list_str(int fid, char* name)
{
    // TODO: Incomplete.

    return -1;
}

// 0x419160
Art* artLock(int fid, CacheEntry** handlePtr)
{
    if (handlePtr == nullptr) {
        return nullptr;
    }

    Art* art = nullptr;
    cacheLock(&gArtCache, fid, (void**)&art, handlePtr);
    return art;
}

// 0x419188
unsigned char* artLockFrameData(int fid, int frame, int direction, CacheEntry** handlePtr)
{
    Art* art;
    ArtFrame* frm;

    art = nullptr;
    if (handlePtr) {
        cacheLock(&gArtCache, fid, (void**)&art, handlePtr);
    }

    if (art != nullptr) {
        frm = artGetFrame(art, frame, direction);
        if (frm != nullptr) {

            return (unsigned char*)frm + sizeof(*frm);
        }
    }

    return nullptr;
}

// 0x4191CC
unsigned char* artLockFrameDataReturningSize(int fid, CacheEntry** handlePtr, int* widthPtr, int* heightPtr)
{
    *handlePtr = nullptr;

    Art* art = nullptr;
    cacheLock(&gArtCache, fid, (void**)&art, handlePtr);

    if (art == nullptr) {
        return nullptr;
    }

    // NOTE: Uninline.
    *widthPtr = artGetWidth(art, 0, 0);
    if (*widthPtr == -1) {
        return nullptr;
    }

    // NOTE: Uninline.
    *heightPtr = artGetHeight(art, 0, 0);
    if (*heightPtr == -1) {
        return nullptr;
    }

    // NOTE: Uninline.
    return artGetFrameData(art, 0, 0);
}

// 0x419260
int artUnlock(CacheEntry* handle)
{
    return cacheUnlock(&gArtCache, handle);
}

// 0x41927C
int artCacheFlush()
{
    return cacheFlush(&gArtCache);
}

// 0x4192B0
int artCopyFileName(int objectType, int id, char* dest)
{
    ArtListDescription* ptr;

    if (objectType < OBJ_TYPE_ITEM || objectType >= OBJ_TYPE_COUNT) {
        return -1;
    }

    ptr = &(gArtListDescriptions[objectType]);

    if (id >= ptr->fileNamesLength) {
        return -1;
    }

    strcpy(dest, ptr->fileNames + id * FILENAME_LENGTH);

    return 0;
}

// 0x419314
int _art_get_code(int animation, int weaponType, char* a3, char* a4)
{
    if (weaponType < 0 || weaponType >= WEAPON_ANIMATION_COUNT) {
        return -1;
    }

    if (animation >= ANIM_TAKE_OUT && animation <= ANIM_FIRE_CONTINUOUS) {
        *a4 = 'c' + (animation - ANIM_TAKE_OUT);
        if (weaponType == WEAPON_ANIMATION_NONE) {
            return -1;
        }

        *a3 = 'd' + (weaponType - 1);
        return 0;
    } else if (animation == ANIM_PRONE_TO_STANDING) {
        *a4 = 'h';
        *a3 = 'c';
        return 0;
    } else if (animation == ANIM_BACK_TO_STANDING) {
        *a4 = 'j';
        *a3 = 'c';
        return 0;
    } else if (animation == ANIM_CALLED_SHOT_PIC) {
        *a4 = 'a';
        *a3 = 'n';
        return 0;
    } else if (animation >= FIRST_SF_DEATH_ANIM) {
        *a4 = 'a' + (animation - FIRST_SF_DEATH_ANIM);
        *a3 = 'r';
        return 0;
    } else if (animation >= FIRST_KNOCKDOWN_AND_DEATH_ANIM) {
        *a4 = 'a' + (animation - FIRST_KNOCKDOWN_AND_DEATH_ANIM);
        *a3 = 'b';
        return 0;
    } else if (animation == ANIM_THROW_ANIM) {
        if (weaponType == WEAPON_ANIMATION_KNIFE) {
            // knife
            *a3 = 'd';
            *a4 = 'm';
        } else if (weaponType == WEAPON_ANIMATION_SPEAR) {
            // spear
            *a3 = 'g';
            *a4 = 'm';
        } else {
            // other -> probably rock or grenade
            *a3 = 'a';
            *a4 = 's';
        }
        return 0;
    } else if (animation == ANIM_DODGE_ANIM) {
        if (weaponType <= 0) {
            *a3 = 'a';
            *a4 = 'n';
        } else {
            *a3 = 'd' + (weaponType - 1);
            *a4 = 'e';
        }
        return 0;
    }

    *a4 = 'a' + animation;
    if (animation <= ANIM_WALK && weaponType > 0) {
        *a3 = 'd' + (weaponType - 1);
        return 0;
    }
    *a3 = 'a';

    return 0;
}

// 0x419428
char* artBuildFilePath(int fid)
{
    // Step 1: Unpack FID components
    int rotation = (fid & 0x70000000) >> 28;
    int aliasedFid = artAliasFid(fid);
    if (aliasedFid != -1) {
        fid = aliasedFid;
    }

    // Clear global buffer
    *_art_name = '\0';

    // Extract FID components
    int id = fid & 0xFFF;
    int animType = FID_ANIM_TYPE(fid);
    int weaponCode = (fid & 0xF000) >> 12;
    int objectType = FID_TYPE(fid);

    // Validate object type
    if (objectType < OBJ_TYPE_ITEM || objectType >= OBJ_TYPE_COUNT) {
        return nullptr;
    }

    // Validate art ID
    if (id >= gArtListDescriptions[objectType].fileNamesLength) {
        return nullptr;
    }

    // Calculate filename offset
    int nameOffset = id * FILENAME_LENGTH;

    // Handle special cases first
    if (objectType == OBJ_TYPE_CRITTER) { // Critters
        char animCode, weaponCodeChar;
        if (_art_get_code(animType, weaponCode, &animCode, &weaponCodeChar) == -1) {
            return nullptr;
        }

        if (rotation != 0) {
            snprintf(_art_name, sizeof(_art_name),
                "%sart\\%s\\%s%c%c.fr%c",
                _cd_path_base,
                gArtListDescriptions[objectType].name,
                gArtListDescriptions[objectType].fileNames + nameOffset,
                animCode,
                weaponCodeChar,
                rotation + '0');
        } else {
            snprintf(_art_name, sizeof(_art_name),
                "%sart\\%s\\%s%c%c.frm",
                _cd_path_base,
                gArtListDescriptions[objectType].name,
                gArtListDescriptions[objectType].fileNames + nameOffset,
                animCode,
                weaponCodeChar);
        }
    } else if (objectType == OBJ_TYPE_HEAD) { // Heads
        char genderCode = _head2[animType];
        if (genderCode == 'f') {
            snprintf(_art_name, sizeof(_art_name),
                "%sart\\%s\\%s%c%c%d.frm",
                _cd_path_base,
                gArtListDescriptions[objectType].name,
                gArtListDescriptions[objectType].fileNames + nameOffset,
                _head1[animType],
                'f',
                weaponCode);
        } else {
            snprintf(_art_name, sizeof(_art_name),
                "%sart\\%s\\%s%c%c.frm",
                _cd_path_base,
                gArtListDescriptions[objectType].name,
                gArtListDescriptions[objectType].fileNames + nameOffset,
                _head1[animType],
                genderCode);
        }
    } else { // All other types
        const char* fileName = gArtListDescriptions[objectType].fileNames + nameOffset;
        char basePath[COMPAT_MAX_PATH];

        // MOD FIX: Handle paths with subdirectories
        if (strchr(fileName, '/') || strchr(fileName, '\\')) {
            // File is in a subdirectory - use direct path
            snprintf(basePath, sizeof(basePath),
                "%sart\\%s\\%s",
                _cd_path_base,
                gArtListDescriptions[objectType].name,
                fileName);
        } else {
            // Standard file - append .frm if needed
            snprintf(basePath, sizeof(basePath),
                "%sart\\%s\\%s",
                _cd_path_base,
                gArtListDescriptions[objectType].name,
                fileName);

            size_t len = strlen(basePath);
            if (len < 4 || compat_stricmp(basePath + len - 4, ".frm") != 0) {
                if (len < sizeof(basePath) - 5) {
                    strcat(basePath, ".frm");
                } else {
                    debugPrint("Path too long: %s", basePath);
                    return nullptr;
                }
            }
        }

        // Copy to global buffer
        strncpy(_art_name, basePath, sizeof(_art_name));
        _art_name[sizeof(_art_name) - 1] = '\0';

        // Normalize path separators (cross-platform fix)
        for (char* p = _art_name; *p; p++) {
            if (*p == '\\') *p = '/';
        }
    }
    return _art_name;
}

// art_read_lst
// 0x419664
static int artReadList(const char* path, char** artListPtr, int* artListSizePtr)
{
    // 1. Show path being read
    showMesageBox("artReadList() reading:");
    showMesageBox(path);
    
    File* stream = fileOpen(path, "rt");
    if (stream == nullptr) {
        showMesageBox("artReadList: fileOpen failed");
        return -1;
    }

    // 2. Get file size
    fileSeek(stream, 0, SEEK_END);
    long size = fileTell(stream);
    fileSeek(stream, 0, SEEK_SET);
    
    char sizeMsg[256];
    snprintf(sizeMsg, sizeof(sizeMsg), "File size: %ld bytes", size);
    showMesageBox(sizeMsg);
    
    // 3. First pass: count non-empty lines
    int count = 0;
    char string[200];
    while (fileReadString(string, sizeof(string), stream)) {
        // Trim whitespace and skip empty lines
        char* p = string;
        while (*p && isspace((unsigned char)*p)) p++;
        if (*p != '\0') count++;
    }
    
    snprintf(sizeMsg, sizeof(sizeMsg), "Non-empty lines: %d", count);
    showMesageBox(sizeMsg);
    
    if (count == 0) {
        showMesageBox("artReadList: No valid entries");
        fileClose(stream);
        return -1;
    }

    // 4. Allocate memory
    fileSeek(stream, 0, SEEK_SET);
    *artListSizePtr = count;
    char* artList = (char*)internal_malloc(FILENAME_LENGTH * count);
    *artListPtr = artList;
    
    if (artList == nullptr) {
        fileClose(stream);
        return -1;
    }

    // 5. Second pass: process lines
    int index = 0;
    while (fileReadString(string, sizeof(string), stream)) {
        // Show raw line
        char rawMsg[256];

        
        // Trim whitespace
        char* start = string;
        while (*start && isspace((unsigned char)*start)) start++;
        
        char* end = start + strlen(start) - 1;
        while (end > start && isspace((unsigned char)*end)) end--;
        *(end + 1) = '\0';
        
        if (*start == '\0') {
            showMesageBox("Skipping empty line");
            continue;
        }
        
        // Trim at first delimiter
        char* brk = strpbrk(start, " ,;\r\t\n");
        if (brk != nullptr) {
            *brk = '\0';
        }
        
        // Validate filename
        if (strlen(start) == 0) {
            showMesageBox("Skipping empty filename");
            continue;
        }
        
        if (strlen(start) >= FILENAME_LENGTH) {

            continue;
        }
        
        // Copy to artList
        strncpy(artList, start, FILENAME_LENGTH - 1);
        artList[FILENAME_LENGTH - 1] = '\0';
        

        
        artList += FILENAME_LENGTH;
        index++;
    }
    
    // 6. Final count adjustment
    *artListSizePtr = index;

    
    fileClose(stream);
    return 0;
}

// 0x419760
int artGetFramesPerSecond(Art* art)
{
    if (art == nullptr) {
        return 10;
    }

    return art->framesPerSecond == 0 ? 10 : art->framesPerSecond;
}

// 0x419778
int artGetActionFrame(Art* art)
{
    return art == nullptr ? -1 : art->actionFrame;
}

// 0x41978C
int artGetFrameCount(Art* art)
{
    return art == nullptr ? -1 : art->frameCount;
}

// 0x4197A0
int artGetWidth(Art* art, int frame, int direction)
{
    ArtFrame* frm;

    frm = artGetFrame(art, frame, direction);
    if (frm == nullptr) {
        return -1;
    }

    return frm->width;
}

// 0x4197B8
int artGetHeight(Art* art, int frame, int direction)
{
    ArtFrame* frm;

    frm = artGetFrame(art, frame, direction);
    if (frm == nullptr) {
        return -1;
    }

    return frm->height;
}

// 0x4197D4
int artGetSize(Art* art, int frame, int direction, int* widthPtr, int* heightPtr)
{
    ArtFrame* frm;

    frm = artGetFrame(art, frame, direction);
    if (frm == nullptr) {
        if (widthPtr != nullptr) {
            *widthPtr = 0;
        }

        if (heightPtr != nullptr) {
            *heightPtr = 0;
        }

        return -1;
    }

    if (widthPtr != nullptr) {
        *widthPtr = frm->width;
    }

    if (heightPtr != nullptr) {
        *heightPtr = frm->height;
    }

    return 0;
}

// 0x419820
int artGetFrameOffsets(Art* art, int frame, int direction, int* xPtr, int* yPtr)
{
    ArtFrame* frm;

    frm = artGetFrame(art, frame, direction);
    if (frm == nullptr) {
        return -1;
    }

    *xPtr = frm->x;
    *yPtr = frm->y;

    return 0;
}

// 0x41984C
int artGetRotationOffsets(Art* art, int rotation, int* xPtr, int* yPtr)
{
    if (art == nullptr) {
        return -1;
    }

    *xPtr = art->xOffsets[rotation];
    *yPtr = art->yOffsets[rotation];

    return 0;
}

// 0x419870
unsigned char* artGetFrameData(Art* art, int frame, int direction)
{
    ArtFrame* frm;

    frm = artGetFrame(art, frame, direction);
    if (frm == nullptr) {
        return nullptr;
    }

    return (unsigned char*)frm + sizeof(*frm);
}

// 0x419880
ArtFrame* artGetFrame(Art* art, int frame, int rotation)
{
    if (rotation < 0 || rotation >= 6) {
        return nullptr;
    }

    if (art == nullptr) {
        return nullptr;
    }

    if (frame < 0 || frame >= art->frameCount) {
        return nullptr;
    }

    ArtFrame* frm = (ArtFrame*)((unsigned char*)art + sizeof(*art) + art->dataOffsets[rotation] + art->padding[rotation]);
    for (int index = 0; index < frame; index++) {
        frm = (ArtFrame*)((unsigned char*)frm + sizeof(*frm) + frm->size + paddingForSize(frm->size));
    }
    return frm;
}

// 0x4198C8
bool artExists(int fid)
{
    bool result = false;

    char* filePath = artBuildFilePath(fid);
    if (filePath != nullptr) {
        int fileSize;
        if (dbGetFileSize(filePath, &fileSize) != -1) {
            result = true;
        }
    }

    return result;
}

// NOTE: Exactly the same implementation as `artExists`.
//
// 0x419930
bool _art_fid_valid(int fid)
{
    bool result = false;

    char* filePath = artBuildFilePath(fid);
    if (filePath != nullptr) {
        int fileSize;
        if (dbGetFileSize(filePath, &fileSize) != -1) {
            result = true;
        }
    }

    return result;
}

// 0x419998
int _art_alias_num(int index)
{
    return _anon_alias[index];
}

// 0x4199AC
int artCritterFidShouldRun(int fid)
{
    if (FID_TYPE(fid) == OBJ_TYPE_CRITTER) {
        return gArtCritterFidShoudRunData[fid & 0xFFF];
    }

    return 0;
}

// 0x4199D4
int artAliasFid(int fid)
{
    int type = FID_TYPE(fid);
    int anim = FID_ANIM_TYPE(fid);
    if (type == OBJ_TYPE_CRITTER) {
        if (anim == ANIM_ELECTRIFY
            || anim == ANIM_BURNED_TO_NOTHING
            || anim == ANIM_ELECTRIFIED_TO_NOTHING
            || anim == ANIM_ELECTRIFY_SF
            || anim == ANIM_BURNED_TO_NOTHING_SF
            || anim == ANIM_ELECTRIFIED_TO_NOTHING_SF
            || anim == ANIM_FIRE_DANCE
            || anim == ANIM_CALLED_SHOT_PIC) {
            // NOTE: Original code is slightly different. It uses many mutually
            // mirrored bitwise operators. Probably result of some macros for
            // getting/setting individual bits on fid.
            return (fid & 0x70000000) | ((anim << 16) & 0xFF0000) | 0x1000000 | (fid & 0xF000) | (_anon_alias[fid & 0xFFF] & 0xFFF);
        }
    }

    return -1;
}

// 0x419A78
static int artCacheGetFileSizeImpl(int fid, int* sizePtr)
{
    int result = -1;

    char* artFilePath = artBuildFilePath(fid);
    if (artFilePath != nullptr) {
        bool loaded = false;
        File* stream = nullptr;

        if (gArtLanguageInitialized) {
            char* pch = strchr(artFilePath, '\\');
            if (pch == nullptr) {
                pch = artFilePath;
            }

            char localizedPath[COMPAT_MAX_PATH];
            snprintf(localizedPath, sizeof(localizedPath), "art\\%s\\%s", gArtLanguage, pch);

            stream = fileOpen(localizedPath, "rb");
        }

        if (stream == nullptr) {
            stream = fileOpen(artFilePath, "rb");
        }

        if (stream != nullptr) {
            Art art;
            if (artReadHeader(&art, stream) == 0) {
                *sizePtr = artGetDataSize(&art);
                result = 0;
            }
            fileClose(stream);
        }
    }

    return result;
}

// 0x419B78
static int artCacheReadDataImpl(int fid, int* sizePtr, unsigned char* data)
{
    int result = -1;

    char* artFileName = artBuildFilePath(fid);
    if (artFileName != nullptr) {
        bool loaded = false;
        if (gArtLanguageInitialized) {
            char* pch = strchr(artFileName, '\\');
            if (pch == nullptr) {
                pch = artFileName;
            }

            char localizedPath[COMPAT_MAX_PATH];
            snprintf(localizedPath, sizeof(localizedPath), "art\\%s\\%s", gArtLanguage, pch);

            if (artRead(localizedPath, data) == 0) {
                loaded = true;
            }
        }

        if (!loaded) {
            if (artRead(artFileName, data) == 0) {
                loaded = true;
            }
        }

        if (loaded) {
            *sizePtr = artGetDataSize((Art*)data);
            result = 0;
        }
    }

    return result;
}

// 0x419C80
static void artCacheFreeImpl(void* ptr)
{
    internal_free(ptr);
}

static int buildFidInternal(unsigned short frmId, unsigned char weaponCode, unsigned char animType, unsigned char objectType, unsigned char rotation)
{
    return ((rotation << 28) & 0x70000000) | (objectType << 24) | ((animType << 16) & 0xFF0000) | ((weaponCode << 12) & 0xF000) | (frmId & 0xFFF);
}

// 0x419C88
int buildFid(int objectType, int frmId, int animType, int weaponCode, int rotation)
{
    // Always use rotation 0 (NE) for non-critters, for certain critter animations.
    // For other critter animations, check if art for the given rotation exists, if not try rotation 1 (E) and if that also doesn't exist, then default to 0 (NE).
    if (objectType != OBJ_TYPE_CRITTER
        || animType == ANIM_FIRE_DANCE
        || animType < ANIM_FALL_BACK
        || animType > ANIM_FALL_FRONT_BLOOD) {
        rotation = ROTATION_NE;
    } else if (!artExists(buildFidInternal(frmId, weaponCode, animType, OBJ_TYPE_CRITTER, rotation))) {
        rotation = rotation != ROTATION_E
                && artExists(buildFidInternal(frmId, weaponCode, animType, OBJ_TYPE_CRITTER, ROTATION_E))
            ? ROTATION_E
            : ROTATION_NE;
    }
    return buildFidInternal(frmId, weaponCode, animType, objectType, rotation);
}

// 0x419D60
static int artReadFrameData(unsigned char* data, File* stream, int count, int* paddingPtr)
{
    unsigned char* ptr = data;
    int padding = 0;
    for (int index = 0; index < count; index++) {
        ArtFrame* frame = (ArtFrame*)ptr;

        if (fileReadInt16(stream, &(frame->width)) == -1)
            return -1;
        if (fileReadInt16(stream, &(frame->height)) == -1)
            return -1;
        if (fileReadInt32(stream, &(frame->size)) == -1)
            return -1;
        if (fileReadInt16(stream, &(frame->x)) == -1)
            return -1;
        if (fileReadInt16(stream, &(frame->y)) == -1)
            return -1;
        if (fileRead(ptr + sizeof(ArtFrame), frame->size, 1, stream) != 1)
            return -1;

        ptr += sizeof(ArtFrame) + frame->size;
        ptr += paddingForSize(frame->size);
        padding += paddingForSize(frame->size);
    }

    *paddingPtr = padding;

    return 0;
}

// 0x419E1C
static int artReadHeader(Art* art, File* stream)
{
    if (fileReadInt32(stream, &(art->field_0)) == -1)
        return -1;
    if (fileReadInt16(stream, &(art->framesPerSecond)) == -1)
        return -1;
    if (fileReadInt16(stream, &(art->actionFrame)) == -1)
        return -1;
    if (fileReadInt16(stream, &(art->frameCount)) == -1)
        return -1;
    if (fileReadInt16List(stream, art->xOffsets, ROTATION_COUNT) == -1)
        return -1;
    if (fileReadInt16List(stream, art->yOffsets, ROTATION_COUNT) == -1)
        return -1;
    if (fileReadInt32List(stream, art->dataOffsets, ROTATION_COUNT) == -1)
        return -1;
    if (fileReadInt32(stream, &(art->dataSize)) == -1)
        return -1;

    // CE: Fix malformed `frm` files with `dataSize` set to 0 in Nevada.
    if (art->dataSize == 0) {
        art->dataSize = fileGetSize(stream);
    }

    return 0;
}

// NOTE: Original function was slightly different, but never used. Basically
// it's a memory allocating variant of `artRead` (which reads data into given
// buffer). This function is useful to load custom `frm` files since `Art` now
// needs more memory then it's on-disk size (due to memory padding).
//
// 0x419EC0
Art* artLoad(const char* path)
{
    File* stream = fileOpen(path, "rb");
    if (stream == nullptr) {
        return nullptr;
    }

    Art header;
    if (artReadHeader(&header, stream) != 0) {
        fileClose(stream);
        return nullptr;
    }

    fileClose(stream);

    unsigned char* data = reinterpret_cast<unsigned char*>(internal_malloc(artGetDataSize(&header)));
    if (data == nullptr) {
        return nullptr;
    }

    if (artRead(path, data) != 0) {
        internal_free(data);
        return nullptr;
    }

    return reinterpret_cast<Art*>(data);
}

// 0x419FC0
int artRead(const char* path, unsigned char* data)
{
    File* stream = fileOpen(path, "rb");
    if (stream == nullptr) {
        return -2;
    }

    Art* art = (Art*)data;
    if (artReadHeader(art, stream) != 0) {
        fileClose(stream);
        return -3;
    }

    int currentPadding = paddingForSize(sizeof(Art));
    int previousPadding = 0;

    for (int index = 0; index < ROTATION_COUNT; index++) {
        art->padding[index] = currentPadding;

        if (index == 0 || art->dataOffsets[index - 1] != art->dataOffsets[index]) {
            art->padding[index] += previousPadding;
            currentPadding += previousPadding;
            if (artReadFrameData(data + sizeof(Art) + art->dataOffsets[index] + art->padding[index], stream, art->frameCount, &previousPadding) != 0) {
                fileClose(stream);
                return -5;
            }
        }
    }

    fileClose(stream);
    return 0;
}

// NOTE: Unused.
//
// 0x41A070
int artWriteFrameData(unsigned char* data, File* stream, int count)
{
    unsigned char* ptr = data;
    for (int index = 0; index < count; index++) {
        ArtFrame* frame = (ArtFrame*)ptr;

        if (fileWriteInt16(stream, frame->width) == -1)
            return -1;
        if (fileWriteInt16(stream, frame->height) == -1)
            return -1;
        if (fileWriteInt32(stream, frame->size) == -1)
            return -1;
        if (fileWriteInt16(stream, frame->x) == -1)
            return -1;
        if (fileWriteInt16(stream, frame->y) == -1)
            return -1;
        if (fileWrite(ptr + sizeof(ArtFrame), frame->size, 1, stream) != 1)
            return -1;

        ptr += sizeof(ArtFrame) + frame->size;
        ptr += paddingForSize(frame->size);
    }

    return 0;
}

// NOTE: Unused.
//
// 0x41A138
int artWriteHeader(Art* art, File* stream)
{
    if (fileWriteInt32(stream, art->field_0) == -1)
        return -1;
    if (fileWriteInt16(stream, art->framesPerSecond) == -1)
        return -1;
    if (fileWriteInt16(stream, art->actionFrame) == -1)
        return -1;
    if (fileWriteInt16(stream, art->frameCount) == -1)
        return -1;
    if (fileWriteInt16List(stream, art->xOffsets, ROTATION_COUNT) == -1)
        return -1;
    if (fileWriteInt16List(stream, art->yOffsets, ROTATION_COUNT) == -1)
        return -1;
    if (fileWriteInt32List(stream, art->dataOffsets, ROTATION_COUNT) == -1)
        return -1;
    if (fileWriteInt32(stream, art->dataSize) == -1)
        return -1;

    return 0;
}

// NOTE: Unused.
//
// 0x41A1E8
int artWrite(const char* path, unsigned char* data)
{
    if (data == nullptr) {
        return -1;
    }

    File* stream = fileOpen(path, "wb");
    if (stream == nullptr) {
        return -1;
    }

    Art* art = (Art*)data;
    if (artWriteHeader(art, stream) == -1) {
        fileClose(stream);
        return -1;
    }

    for (int index = 0; index < ROTATION_COUNT; index++) {
        if (index == 0 || art->dataOffsets[index - 1] != art->dataOffsets[index]) {
            if (artWriteFrameData(data + sizeof(Art) + art->dataOffsets[index] + art->padding[index], stream, art->frameCount) != 0) {
                fileClose(stream);
                return -1;
            }
        }
    }

    fileClose(stream);
    return 0;
}

static int artGetDataSize(Art* art)
{
    int dataSize = sizeof(*art) + art->dataSize;

    for (int index = 0; index < ROTATION_COUNT; index++) {
        if (index == 0 || art->dataOffsets[index - 1] != art->dataOffsets[index]) {
            // Assume worst case - every frame is unaligned and need
            // max padding.
            dataSize += (sizeof(int) - 1) * art->frameCount;
        }
    }

    return dataSize;
}

static int paddingForSize(int size)
{
    return (sizeof(int) - size % sizeof(int)) % sizeof(int);
}

FrmImage::FrmImage()
{
    _key = nullptr;
    _data = nullptr;
    _width = 0;
    _height = 0;
}

FrmImage::~FrmImage()
{
    unlock();
}

bool FrmImage::lock(unsigned int fid)
{
    if (isLocked()) {
        return false;
    }

    _data = artLockFrameDataReturningSize(fid, &_key, &_width, &_height);
    if (!_data) {
        return false;
    }

    return true;
}

void FrmImage::unlock()
{
    if (isLocked()) {
        artUnlock(_key);
        _key = nullptr;
        _data = nullptr;
        _width = 0;
        _height = 0;
    }
}

} // namespace fallout
