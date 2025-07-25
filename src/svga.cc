#include "svga.h"

#include <limits.h>
#include <string.h>

#include <SDL.h>

#include "config.h"
#include "draw.h"
#include "interface.h"
#include "memory.h"
#include "mouse.h"
#include "scan_unimplemented.h"
#include "settings.h"
#include "sfall_config.h"
#include "win32.h"
#include "window_manager.h"
#include "window_manager_private.h"

namespace fallout {

static bool createRenderer(int width, int height);
static void destroyRenderer();

static bool gStretchEnabled = true;
static bool gPreserveAspect = true; // used internally for stretching
static bool gUserAspectPreference = true; // store user preference for restore
static bool gHighQuality = false; // maybe false by default
static bool gSquarePixels = false;

static int gContentWidth = 800;
static int gContentHeight = 500;

// screen rect
Rect _scr_size;

// 0x6ACA18
void (*_scr_blit)(unsigned char* src, int src_pitch, int a3, int src_x, int src_y, int src_width, int src_height, int dest_x, int dest_y) = _GNW95_ShowRect;

// 0x6ACA1C
void (*_zero_mem)() = nullptr;

SDL_Window* gSdlWindow = nullptr;
SDL_Surface* gSdlSurface = nullptr;
SDL_Renderer* gSdlRenderer = nullptr;
SDL_Texture* gSdlTexture = nullptr;
SDL_Surface* gSdlTextureSurface = nullptr;

// TODO: Remove once migration to update-render cycle is completed.
FpsLimiter sharedFpsLimiter;

// 0x4CAD08
int _init_mode_320_200()
{
    return _GNW95_init_mode_ex(320, 200, 8);
}

// 0x4CAD40
int _init_mode_320_400()
{
    return _GNW95_init_mode_ex(320, 400, 8);
}

// 0x4CAD5C
int _init_mode_640_480_16()
{
    return -1;
}

// 0x4CAD64
int _init_mode_640_480()
{
    return _init_vesa_mode(640, 480);
}

// 0x4CAD94
int _init_mode_640_400()
{
    return _init_vesa_mode(640, 400);
}

// 0x4CADA8
int _init_mode_800_600()
{
    return _init_vesa_mode(800, 600);
}

// 0x4CADBC
int _init_mode_1024_768()
{
    return _init_vesa_mode(1024, 768);
}

// 0x4CADD0
int _init_mode_1280_1024()
{
    return _init_vesa_mode(1280, 1024);
}

// 0x4CADF8
void _get_start_mode_()
{
}

// 0x4CADFC
void _zero_vid_mem()
{
    if (_zero_mem) {
        _zero_mem();
    }
}

void restoreUserAspectPreference()
{
    gPreserveAspect = gUserAspectPreference;
}

bool gameIsWidescreen()
{
    return (settings.graphics.game_width >= 800 && settings.graphics.game_height >= 500 && settings.graphics.widescreen);
}

// 0x4CAE1C
int _GNW95_init_mode_ex(int width, int height, int bpp)
{
    bool fullscreen = true;

    width = settings.graphics.game_width;
    height = settings.graphics.game_height;
    fullscreen = settings.graphics.fullscreen;

    configGetBool(&gSfallConfig, SFALL_CONFIG_MISC_KEY, SFALL_CONFIG_IFACE_BAR_MODE, &gInterfaceBarMode);
    configGetInt(&gSfallConfig, SFALL_CONFIG_MISC_KEY, SFALL_CONFIG_IFACE_BAR_WIDTH, &gInterfaceBarWidth);
    configGetInt(&gSfallConfig, SFALL_CONFIG_MISC_KEY, SFALL_CONFIG_IFACE_BAR_SIDE_ART, &gInterfaceSidePanelsImageId);
    configGetBool(&gSfallConfig, SFALL_CONFIG_MISC_KEY, SFALL_CONFIG_IFACE_BAR_SIDES_ORI, &gInterfaceSidePanelsExtendFromScreenEdge);

    // setting for stretching - later
    gStretchEnabled = settings.graphics.stretch_enabled;
    gPreserveAspect = settings.graphics.preserve_aspect;
    gUserAspectPreference = settings.graphics.preserve_aspect; // back up the user preference for restoring after using game screen
    gHighQuality = settings.graphics.high_quality;
    gSquarePixels = settings.graphics.square_pixels;

    if (_GNW95_init_window(width, height, fullscreen) == -1) {
        return -1;
    }

    if (directDrawInit(width, height, bpp) == -1) {
        return -1;
    }

    // macOS seems to require dequeuing NSApp events in order for window to
    // become visible. There is no concrete number of calls required to make
    // it happen. Sadly there is no particular event to watch for because SDL
    // marks window as shown immediately after creation (see
    // `SDL_FinishWindowCreation`).
    for (int i = 0; i < 10; i++) {
        SDL_PumpEvents();
    }

    _scr_size.left = 0;
    _scr_size.top = 0;
    _scr_size.right = width - 1;
    _scr_size.bottom = height - 1;

    _mouse_blit_trans = nullptr;
    _scr_blit = _GNW95_ShowRect;
    _zero_mem = _GNW95_zero_vid_mem;
    _mouse_blit = _GNW95_ShowRect;

    return 0;
}

// 0x4CAECC
int _init_vesa_mode(int width, int height)
{
    return _GNW95_init_mode_ex(width, height, 8);
}

// 0x4CAEDC
int _GNW95_init_window(int width, int height, bool fullscreen)
{
    if (gSdlWindow == nullptr) {
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");

        Uint32 windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI;

        if (fullscreen) {
            windowFlags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
        }

        gSdlWindow = SDL_CreateWindow(gProgramWindowTitle, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, width, height, windowFlags);
        if (gSdlWindow == nullptr) {
            return -1;
        }
        int actualWidth = 0, actualHeight = 0;
        SDL_GetWindowSize(gSdlWindow, &actualWidth, &actualHeight);

        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, gHighQuality ? "1" : "0");

        if (!createRenderer(width, height)) {
            destroyRenderer();

            SDL_DestroyWindow(gSdlWindow);
            gSdlWindow = nullptr;

            return -1;
        }
    }

    return 0;
}

// 0x4CAF9C
int directDrawInit(int width, int height, int bpp)
{
    if (gSdlSurface != nullptr) {
        unsigned char* palette = directDrawGetPalette();
        directDrawFree();

        if (directDrawInit(width, height, bpp) == -1) {
            return -1;
        }

        directDrawSetPalette(palette);

        return 0;
    }

    gSdlSurface = SDL_CreateRGBSurface(0, width, height, bpp, 0, 0, 0, 0);

    SDL_Color colors[256];
    for (int index = 0; index < 256; index++) {
        colors[index].r = index;
        colors[index].g = index;
        colors[index].b = index;
        colors[index].a = 255;
    }

    SDL_SetPaletteColors(gSdlSurface->format->palette, colors, 0, 256);

    return 0;
}

// 0x4CB1B0
void directDrawFree()
{
    if (gSdlSurface != nullptr) {
        SDL_FreeSurface(gSdlSurface);
        gSdlSurface = nullptr;
    }
}

// 0x4CB310
void directDrawSetPaletteInRange(unsigned char* palette, int start, int count)
{
    if (gSdlSurface != nullptr && gSdlSurface->format->palette != nullptr) {
        SDL_Color colors[256];

        if (count != 0) {
            for (int index = 0; index < count; index++) {
                colors[index].r = palette[index * 3] << 2;
                colors[index].g = palette[index * 3 + 1] << 2;
                colors[index].b = palette[index * 3 + 2] << 2;
                colors[index].a = 255;
            }
        }

        SDL_SetPaletteColors(gSdlSurface->format->palette, colors, start, count);
        SDL_BlitSurface(gSdlSurface, nullptr, gSdlTextureSurface, nullptr);
    }
}

// 0x4CB568
void directDrawSetPalette(unsigned char* palette)
{
    if (gSdlSurface != nullptr && gSdlSurface->format->palette != nullptr) {
        SDL_Color colors[256];

        for (int index = 0; index < 256; index++) {
            colors[index].r = palette[index * 3] << 2;
            colors[index].g = palette[index * 3 + 1] << 2;
            colors[index].b = palette[index * 3 + 2] << 2;
            colors[index].a = 255;
        }

        SDL_SetPaletteColors(gSdlSurface->format->palette, colors, 0, 256);
        SDL_BlitSurface(gSdlSurface, nullptr, gSdlTextureSurface, nullptr);
    }
}

// 0x4CB68C
unsigned char* directDrawGetPalette()
{
    // 0x6ACA24
    static unsigned char palette[768];

    if (gSdlSurface != nullptr && gSdlSurface->format->palette != nullptr) {
        SDL_Color* colors = gSdlSurface->format->palette->colors;

        for (int index = 0; index < 256; index++) {
            SDL_Color* color = &(colors[index]);
            palette[index * 3] = color->r >> 2;
            palette[index * 3 + 1] = color->g >> 2;
            palette[index * 3 + 2] = color->b >> 2;
        }
    }

    return palette;
}

// 0x4CB850
void _GNW95_ShowRect(unsigned char* src, int srcPitch, int _, int srcX, int srcY, int srcWidth, int srcHeight, int destX, int destY)
{
    blitBufferToBuffer(src + srcPitch * srcY + srcX, srcWidth, srcHeight, srcPitch, (unsigned char*)gSdlSurface->pixels + gSdlSurface->pitch * destY + destX, gSdlSurface->pitch);

    SDL_Rect srcRect;
    srcRect.x = destX;
    srcRect.y = destY;
    srcRect.w = srcWidth;
    srcRect.h = srcHeight;

    SDL_Rect destRect;
    destRect.x = destX;
    destRect.y = destY;
    SDL_BlitSurface(gSdlSurface, &srcRect, gSdlTextureSurface, &destRect);
}

// Clears drawing surface.
//
// 0x4CBBC8
void _GNW95_zero_vid_mem()
{
    if (!gProgramIsActive) {
        return;
    }

    unsigned char* surface = (unsigned char*)gSdlSurface->pixels;
    for (int y = 0; y < gSdlSurface->h; y++) {
        memset(surface, 0, gSdlSurface->w);
        surface += gSdlSurface->pitch;
    }

    SDL_BlitSurface(gSdlSurface, nullptr, gSdlTextureSurface, nullptr);
}

int screenGetWidth()
{
    // TODO: Make it on par with _xres;
    return rectGetWidth(&_scr_size);
}

int screenGetHeight()
{
    // TODO: Make it on par with _yres.
    return rectGetHeight(&_scr_size);
}

int screenGetVisibleHeight()
{
    int windowBottomMargin = 0;

    if (!gInterfaceBarMode) {
        windowBottomMargin = INTERFACE_BAR_HEIGHT;
    }
    return screenGetHeight() - windowBottomMargin;
}

static bool createRenderer(int width, int height)
{
    gSdlRenderer = SDL_CreateRenderer(gSdlWindow, -1, 0);
    if (gSdlRenderer == nullptr) {
        return false;
    }

    gSdlTexture = SDL_CreateTexture(gSdlRenderer, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STREAMING, width, height);
    if (gSdlTexture == nullptr) {
        SDL_DestroyRenderer(gSdlRenderer);
        gSdlRenderer = nullptr;
        return false;
    }

    Uint32 format;
    if (SDL_QueryTexture(gSdlTexture, &format, nullptr, nullptr, nullptr) != 0) {
        SDL_DestroyTexture(gSdlTexture);
        SDL_DestroyRenderer(gSdlRenderer);
        gSdlTexture = nullptr;
        gSdlRenderer = nullptr;
        return false;
    }

    if (gSdlTextureSurface != nullptr) {
        SDL_FreeSurface(gSdlTextureSurface);
    }

    gSdlTextureSurface = SDL_CreateRGBSurfaceWithFormat(0, width, height, SDL_BITSPERPIXEL(format), format);
    if (gSdlTextureSurface == nullptr) {
        SDL_DestroyTexture(gSdlTexture);
        SDL_DestroyRenderer(gSdlRenderer);
        gSdlTexture = nullptr;
        gSdlRenderer = nullptr;
        return false;
    }

    return true;
}

static void destroyRenderer()
{
    if (gSdlTextureSurface != nullptr) {
        SDL_FreeSurface(gSdlTextureSurface);
        gSdlTextureSurface = nullptr;
    }

    if (gSdlTexture != nullptr) {
        SDL_DestroyTexture(gSdlTexture);
        gSdlTexture = nullptr;
    }

    if (gSdlRenderer != nullptr) {
        SDL_DestroyRenderer(gSdlRenderer);
        gSdlRenderer = nullptr;
    }
}

void handleWindowSizeChanged()
{
    static bool isResizing = false;
    if (isResizing)
        return;
    isResizing = true;

    // Save palette by copying data (not just pointer)
    unsigned char* originalPalette = directDrawGetPalette();
    unsigned char paletteCopy[256 * 3]; // Standard 8-bit palette size
    if (originalPalette) {
        memcpy(paletteCopy, originalPalette, sizeof(paletteCopy));
    }

    // Tear down existing renderer
    destroyRenderer();
    directDrawFree();

    // Resize window (no error check possible - SDL doesn't return status)
    int newWidth = settings.graphics.game_width;
    int newHeight = settings.graphics.game_height;
    SDL_SetWindowSize(gSdlWindow, newWidth, newHeight);
    _scr_size = { 0, 0, newWidth - 1, newHeight - 1 };

    // Reinitialize drawing surface
    directDrawInit(newWidth, newHeight, 8);
    _zero_vid_mem();

    // Restore palette if we had one
    if (originalPalette) {
        directDrawSetPalette(paletteCopy);
    }

    // Recreate renderer
    createRenderer(newWidth, newHeight);

    isResizing = false;
}

void resizeContent(int width, int height)
{
    gContentWidth = width;
    gContentHeight = height;
    renderPresent();
}

void resizeContent(int width, int height, bool preserveAspect)
{
    gContentWidth = width;
    gContentHeight = height;
    gPreserveAspect = preserveAspect;
    renderPresent();
}

void renderPresent()
{
    // Get physical pixel size of the window (DPI-aware)
    int renderW, renderH;
    SDL_GetRendererOutputSize(gSdlRenderer, &renderW, &renderH);

    // Get logical window size
    int windowW, windowH;
    SDL_GetWindowSize(gSdlWindow, &windowW, &windowH);

    // Calculate scale factors for DPI
    float scaleX = (float)renderW / windowW;
    float scaleY = (float)renderH / windowH;
    float dpiScale = (scaleX < scaleY) ? scaleX : scaleY; // Use min scale for consistency

    // Get logical rendering size fallback
    int logicalW, logicalH;
    SDL_RenderGetLogicalSize(gSdlRenderer, &logicalW, &logicalH);
    if (logicalW == 0)
        logicalW = gContentWidth;
    if (logicalH == 0)
        logicalH = gContentHeight;

    SDL_Rect srcRect, destRect;

    if (gStretchEnabled) {
        int contentOffsetX = (gSdlTextureSurface->w - gContentWidth) / 2;
        int contentOffsetY = (gSdlTextureSurface->h - gContentHeight) / 2;

        srcRect = {
            contentOffsetX,
            contentOffsetY,
            gContentWidth,
            gContentHeight
        };

        // INTEGER SCALING MODE (with retina support)
        if (gSquarePixels) {
            // Use float for scale to support fractional values
            float scale;

            if (dpiScale > 1.0f) {
                // High DPI: fractional scaling
                float maxScaleX = (float)renderW / gContentWidth;
                float maxScaleY = (float)renderH / gContentHeight;
                scale = (maxScaleX < maxScaleY) ? maxScaleX : maxScaleY;

                // Snap to 0.5 increments for better quality
                scale = floorf(scale * 2.0f) / 2.0f;
                // Ensure minimum scale of 0.5
                if (scale < 0.5f)
                    scale = 0.5f;
            } else {
                // Standard display: integer scaling
                int maxScaleX = renderW / gContentWidth;
                int maxScaleY = renderH / gContentHeight;
                scale = (maxScaleX < maxScaleY) ? maxScaleX : maxScaleY;
                // Ensure minimum scale of 1
                if (scale < 1)
                    scale = 1;
            }

            // Calculate scaled dimensions
            int scaledWidth = static_cast<int>(gContentWidth * scale);
            int scaledHeight = static_cast<int>(gContentHeight * scale);

            // Center the scaled content
            destRect = {
                (renderW - scaledWidth) / 2,
                (renderH - scaledHeight) / 2,
                scaledWidth,
                scaledHeight
            };
        }
        // ASPECT-RATIO PRESERVED MODE
        else if (gPreserveAspect) {
            float contentAspect = (float)gContentWidth / gContentHeight;
            float windowAspect = (float)renderW / renderH;

            if (windowAspect > contentAspect) {
                destRect.h = renderH;
                destRect.w = (int)(renderH * contentAspect);
                destRect.x = (renderW - destRect.w) / 2;
                destRect.y = 0;
            } else {
                destRect.w = renderW;
                destRect.h = (int)(renderW / contentAspect);
                destRect.x = 0;
                destRect.y = (renderH - destRect.h) / 2;
            }
        }
        // STRETCH TO FILL MODE
        else {
            // Stretch to fill entire window
            destRect = { 0, 0, renderW, renderH };
        }

        SDL_UpdateTexture(gSdlTexture, &srcRect,
            (uint8_t*)gSdlTextureSurface->pixels + contentOffsetY * gSdlTextureSurface->pitch + contentOffsetX * 4,
            gSdlTextureSurface->pitch);

    } else {
        // Stretching disabled — display original size scaled for DPI, centered
        srcRect = { 0, 0, gSdlTextureSurface->w, gSdlTextureSurface->h };

        destRect = {
            (renderW - (int)(gSdlTextureSurface->w * scaleX)) / 2,
            (renderH - (int)(gSdlTextureSurface->h * scaleY)) / 2,
            (int)(gSdlTextureSurface->w * scaleX),
            (int)(gSdlTextureSurface->h * scaleY)
        };

        SDL_UpdateTexture(gSdlTexture, nullptr,
            gSdlTextureSurface->pixels,
            gSdlTextureSurface->pitch);
    }

    SDL_SetRenderDrawColor(gSdlRenderer, 0, 0, 255, 255);
    SDL_RenderClear(gSdlRenderer);

    SDL_RenderCopy(gSdlRenderer, gSdlTexture, &srcRect, &destRect);
    SDL_RenderPresent(gSdlRenderer);
}

} // namespace fallout
