/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Mateusz Szuła
 *
 * src/platform/3ds/SDL/SDL.h — SDL compatibility header for 3DS.
 *
 * Minimal SDL API definitions. Implementation in SDL_compat.c.
 * Real rendering done by video_3ds_gl.c using citro3d.
 */

#ifndef SDL_H_3DS_COMPAT
#define SDL_H_3DS_COMPAT

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- SDL Init Flags --------------------------------------------------- */
#define SDL_INIT_VIDEO       0x00000020
#define SDL_INIT_AUDIO       0x00000010
#define SDL_INIT_TIMER       0x00000001
#define SDL_INIT_EVERYTHING  0x0000FFFF

/* ---- SDL Window Flags ------------------------------------------------- */
#define SDL_WINDOW_FULLSCREEN    0x00000001
#define SDL_WINDOW_OPENGL        0x00000002
#define SDL_WINDOW_SHOWN         0x00000004
#define SDL_WINDOW_RESIZABLE     0x00000020

/* ---- SDL Renderer Flags ----------------------------------------------- */
#define SDL_RENDERER_ACCELERATED 0x00000002
#define SDL_RENDERER_PRESENTVSYNC 0x00000004

/* ---- SDL Pixel Formats ------------------------------------------------ */
#define SDL_PIXELFORMAT_ARGB8888 0x16362004
#define SDL_PIXELFORMAT_RGB888   0x16161804
#define SDL_PIXELFORMAT_RGBA8888 0x16462004
#define SDL_PIXELFORMAT_INDEX8   0x10100801

/* ---- SDL Texture Access ----------------------------------------------- */
#define SDL_TEXTUREACCESS_STREAMING 0x00000001

/* ---- SDL Blend Modes -------------------------------------------------- */
#define SDL_BLENDMODE_NONE  0
#define SDL_BLENDMODE_BLEND 1

/* ---- SDL Scancode ----------------------------------------------------- */
typedef enum {
    SDL_SCANCODE_ESCAPE = 41,
    SDL_SCANCODE_F1 = 58,
    SDL_SCANCODE_F2 = 59,
    SDL_SCANCODE_F3 = 60,
    SDL_SCANCODE_F4 = 61,
    SDL_SCANCODE_F5 = 62,
    SDL_SCANCODE_F10 = 67,
    SDL_SCANCODE_SPACE = 44,
    SDL_SCANCODE_RETURN = 40,
    SDL_SCANCODE_LSHIFT = 225,
    SDL_SCANCODE_RSHIFT = 229,
    SDL_SCANCODE_UP = 82,
    SDL_SCANCODE_DOWN = 81,
    SDL_SCANCODE_LEFT = 80,
    SDL_SCANCODE_RIGHT = 79,
} SDL_Scancode;

/* ---- SDL KeyCode ------------------------------------------------------ */
typedef enum {
    SDLK_ESCAPE = 27,
    SDLK_SPACE = 32,
    SDLK_RETURN = 13,
    SDLK_TAB = 9,
    SDLK_BACKSPACE = 8,
    SDLK_F1 = 1073741882,
    SDLK_F2 = 1073741883,
    SDLK_F3 = 1073741884,
    SDLK_F4 = 1073741885,
    SDLK_F5 = 1073741886,
    SDLK_F8 = 1073741889,
    SDLK_F9 = 1073741890,
    SDLK_F10 = 1073741891,
    SDLK_F11 = 1073741892,
    SDLK_F12 = 1073741893,
    SDLK_KP_ENTER = 1073741912,
    SDLK_AC_BACK = 1073742094,
    SDLK_SCANCODE_MASK = (1<<30),
} SDL_KeyCode;

typedef SDL_KeyCode SDL_Keycode;

/* ---- SDL Constants ---------------------------------------------------- */
#define SDL_DISABLE 0
#define SDL_ENABLE 1

/* Window event IDs */
#define SDL_WINDOWEVENT_CLOSE 14
#define SDL_WINDOWEVENT_RESIZED 5

/* Mouse buttons */
#define SDL_BUTTON_LEFT 1
#define SDL_BUTTON_RIGHT 3

/* Message box flags */
#define SDL_MESSAGEBOX_ERROR 0x00000010

/* Hints */
#define SDL_HINT_TOUCH_MOUSE_EVENTS "SDL_TOUCH_MOUSE_EVENTS"

/* ---- SDL Events ------------------------------------------------------- */
typedef enum {
    SDL_QUIT = 0x100,
    SDL_WINDOWEVENT = 0x200,
    SDL_KEYDOWN = 0x300,
    SDL_KEYUP = 0x301,
    SDL_TEXTINPUT = 0x303,
    SDL_MOUSEMOTION = 0x400,
    SDL_MOUSEBUTTONDOWN = 0x401,
    SDL_MOUSEBUTTONUP = 0x402,
    SDL_FINGERDOWN = 0x700,
    SDL_FINGERUP = 0x701,
    SDL_FINGERMOTION = 0x702,
    SDL_CONTROLLERBUTTONDOWN = 0x650,
    SDL_CONTROLLERDEVICEADDED = 0x651,
    SDL_CONTROLLERDEVICEREMOVED = 0x652,
} SDL_EventType;

typedef struct SDL_Keysym {
    SDL_Scancode scancode;
    SDL_KeyCode sym;
    uint16_t mod;
} SDL_Keysym;

typedef struct SDL_KeyboardEvent {
    uint32_t type;
    uint32_t timestamp;
    uint8_t state;
    uint8_t repeat;
    SDL_Keysym keysym;
} SDL_KeyboardEvent;

typedef struct SDL_MouseButtonEvent {
    uint32_t type;
    uint32_t timestamp;
    uint32_t windowID;
    uint8_t button;
    uint8_t state;
    int32_t x;
    int32_t y;
} SDL_MouseButtonEvent;

typedef struct SDL_MouseMotionEvent {
    uint32_t type;
    uint32_t timestamp;
    int32_t x;
    int32_t y;
} SDL_MouseMotionEvent;

typedef struct SDL_QuitEvent {
    uint32_t type;
    uint32_t timestamp;
} SDL_QuitEvent;

typedef union SDL_Event {
    uint32_t type;
    SDL_KeyboardEvent key;
    SDL_MouseButtonEvent button;
    SDL_MouseMotionEvent motion;
    SDL_QuitEvent quit;
    struct {
        uint32_t type;
        uint32_t timestamp;
        uint32_t windowID;
        uint8_t event;
        int32_t data1;
        int32_t data2;
    } window;
    struct {
        uint32_t type;
        uint32_t timestamp;
        char text[32];
    } text;
    struct {
        uint32_t type;
        uint32_t timestamp;
        int64_t touchId;
        int64_t fingerId;
        float x;
        float y;
        float dx;
        float dy;
        float pressure;
    } tfinger;
} SDL_Event;

/* Additional touch/finger types */
typedef int64_t SDL_FingerID;
typedef int64_t SDL_TouchID;

/* ---- SDL Types -------------------------------------------------------- */
typedef struct SDL_Window SDL_Window;
typedef struct SDL_Renderer SDL_Renderer;
typedef struct SDL_Texture SDL_Texture;

/* SDL basic types */
typedef uint8_t Uint8;
typedef uint16_t Uint16;
typedef uint32_t Uint32;
typedef int32_t Sint32;

/* SDL Color */
typedef struct SDL_Color {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
} SDL_Color;

typedef struct SDL_Rect {
    int x, y;
    int w, h;
} SDL_Rect;

typedef struct SDL_Surface {
    uint32_t flags;
    void *format;
    int w, h;
    int pitch;
    void *pixels;
    void *userdata;
    int locked;
    void *lock_data;
    SDL_Rect clip_rect;
    void *map;
    int refcount;
} SDL_Surface;

typedef struct SDL_PixelFormat {
    uint32_t format;
    void *palette;
    uint8_t BitsPerPixel;
    uint8_t BytesPerPixel;
    uint32_t Rmask, Gmask, Bmask, Amask;
} SDL_PixelFormat;

typedef struct SDL_RWops SDL_RWops;

/* ---- SDL Audio -------------------------------------------------------- */
typedef struct SDL_AudioSpec {
    int freq;
    uint16_t format;
    uint8_t channels;
    uint8_t silence;
    uint16_t samples;
    uint32_t size;
    void (*callback)(void *userdata, uint8_t *stream, int len);
    void *userdata;
} SDL_AudioSpec;

#define AUDIO_S16SYS 0x8010

/* ---- SDL Functions ---------------------------------------------------- */

/* Init/Quit */
int SDL_Init(uint32_t flags);
void SDL_Quit(void);
const char* SDL_GetError(void);

/* Window/Renderer (stubs - rendering done in video_3ds_gl.c) */
SDL_Window* SDL_CreateWindow(const char *title, int x, int y, int w, int h, uint32_t flags);
void SDL_DestroyWindow(SDL_Window *window);
SDL_Renderer* SDL_CreateRenderer(SDL_Window *window, int index, uint32_t flags);
void SDL_DestroyRenderer(SDL_Renderer *renderer);
SDL_Texture* SDL_CreateTexture(SDL_Renderer *renderer, uint32_t format, int access, int w, int h);
void SDL_DestroyTexture(SDL_Texture *texture);
int SDL_UpdateTexture(SDL_Texture *texture, const SDL_Rect *rect, const void *pixels, int pitch);
int SDL_RenderCopy(SDL_Renderer *renderer, SDL_Texture *texture, const SDL_Rect *srcrect, const SDL_Rect *dstrect);
void SDL_RenderPresent(SDL_Renderer *renderer);
int SDL_SetRenderDrawColor(SDL_Renderer *renderer, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
int SDL_RenderClear(SDL_Renderer *renderer);
int SDL_RenderFillRect(SDL_Renderer *renderer, const SDL_Rect *rect);
int SDL_SetTextureBlendMode(SDL_Texture *texture, int blendMode);
int SDL_LockTexture(SDL_Texture *texture, const SDL_Rect *rect, void **pixels, int *pitch);
void SDL_UnlockTexture(SDL_Texture *texture);

/* Events */
int SDL_PollEvent(SDL_Event *event);
const uint8_t* SDL_GetKeyboardState(int *numkeys);
int SDL_PushEvent(SDL_Event *event);

/* Input */
int SDL_ShowCursor(int toggle);
uint32_t SDL_GetMouseState(int *x, int *y);

/* Timer */
void SDL_Delay(uint32_t ms);
uint32_t SDL_GetTicks(void);

/* Memory */
void* SDL_malloc(size_t size);
void SDL_free(void *ptr);
void* SDL_memcpy(void *dst, const void *src, size_t len);
void* SDL_memset(void *dst, int c, size_t len);

/* Environment and text input */
char* SDL_getenv(const char *name);
int SDL_setenv(const char *name, const char *value, int overwrite);
void SDL_StartTextInput(void);
void SDL_StopTextInput(void);
int SDL_SetHint(const char *name, const char *value);
char* SDL_GetBasePath(void);
int SDL_ShowSimpleMessageBox(uint32_t flags, const char *title, const char *message, SDL_Window *window);

/* Surface */
SDL_Surface* SDL_CreateRGBSurface(uint32_t flags, int width, int height, int depth,
                                   uint32_t Rmask, uint32_t Gmask, uint32_t Bmask, uint32_t Amask);
SDL_Surface* SDL_CreateRGBSurfaceWithFormatFrom(void *pixels, int w, int h, int depth, int pitch, uint32_t format);
void SDL_FreeSurface(SDL_Surface *surface);
int SDL_LockSurface(SDL_Surface *surface);
void SDL_UnlockSurface(SDL_Surface *surface);
SDL_Surface* SDL_ConvertSurfaceFormat(SDL_Surface *src, uint32_t pixel_format, uint32_t flags);
int SDL_SetPaletteColors(void *palette, const SDL_Color *colors, int firstcolor, int ncolors);
int SDL_SaveBMP(SDL_Surface *surface, const char *file);

/* Audio */
int SDL_OpenAudio(SDL_AudioSpec *desired, SDL_AudioSpec *obtained);
void SDL_CloseAudio(void);
void SDL_PauseAudio(int pause_on);
void SDL_LockAudio(void);
void SDL_UnlockAudio(void);
void SDL_MixAudio(uint8_t *dst, const uint8_t *src, uint32_t len, int volume);

/* Additional SDL audio types */
typedef struct SDL_AudioCVT {
    int needed;
    Uint16 src_format;
    Uint16 dst_format;
    double rate_incr;
    Uint8 *buf;
    int len;
    int len_cvt;
    int len_mult;
    double len_ratio;
    void *filters[10];
    int filter_index;
} SDL_AudioCVT;

/* SDL RWops - file I/O */
SDL_RWops* SDL_RWFromFile(const char *file, const char *mode);
SDL_RWops* SDL_RWFromConstMem(const void *mem, int size);
int64_t SDL_RWsize(SDL_RWops *context);
int64_t SDL_RWseek(SDL_RWops *context, int64_t offset, int whence);
size_t SDL_RWread(SDL_RWops *context, void *ptr, size_t size, size_t maxnum);
size_t SDL_RWwrite(SDL_RWops *context, const void *ptr, size_t size, size_t num);
int SDL_RWclose(SDL_RWops *context);

/* SDL Audio loading */
SDL_AudioSpec* SDL_LoadWAV_RW(SDL_RWops *src, int freesrc, SDL_AudioSpec *spec, Uint8 **audio_buf, Uint32 *audio_len);
void SDL_FreeWAV(Uint8 *audio_buf);
int SDL_BuildAudioCVT(SDL_AudioCVT *cvt, Uint16 src_format, Uint8 src_channels, int src_rate,
                      Uint16 dst_format, Uint8 dst_channels, int dst_rate);
int SDL_ConvertAudio(SDL_AudioCVT *cvt);

#define RW_SEEK_SET 0
#define RW_SEEK_CUR 1
#define RW_SEEK_END 2

#ifdef __cplusplus
}
#endif

#endif /* SDL_H_3DS_COMPAT */
