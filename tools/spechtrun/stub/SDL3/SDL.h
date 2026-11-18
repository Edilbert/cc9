/* Minimal headless SDL3 stub so specht.c can be linked without SDL.
   Only used by the automatic test runner (spechtrun). */
#ifndef SDL_STUB_H
#define SDL_STUB_H
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
typedef uint16_t Uint16; typedef uint32_t Uint32; typedef uint64_t Uint64;
typedef int SDL_Scancode; typedef int SDL_Keycode; typedef int SDL_Keymod;
typedef int SDL_KeyboardID; typedef int SDL_WindowID; typedef int SDL_EventType;
typedef struct { int x,y,w,h; } SDL_Rect;
typedef struct { float x,y,w,h; } SDL_FRect;
typedef struct { SDL_EventType type; SDL_Scancode scancode; SDL_Keycode key;
                 SDL_Keymod mod; Uint16 raw; } SDL_KeyboardEvent;
typedef union { SDL_EventType type; SDL_KeyboardEvent key; } SDL_Event;
typedef struct SDL_Window SDL_Window;
typedef struct SDL_Renderer SDL_Renderer;
typedef struct SDL_Texture SDL_Texture;
static char sdl_stub_dummy[16];
#define SDL_INIT_VIDEO 0x20
#define SDL_WINDOW_OPENGL 2
#define SDL_PIXELFORMAT_RGB565 1
#define SDL_TEXTUREACCESS_STATIC 0
#define SDL_BLENDMODE_NONE 0
#define SDL_EVENT_QUIT 0x100
#define SDL_EVENT_KEY_DOWN 0x300
#define SDL_EVENT_KEY_UP 0x301
#define SDLK_INSERT 73
#define SDL_KMOD_LSHIFT 1
#define SDL_KMOD_RSHIFT 2
#define SDL_KMOD_SHIFT 3
#define SDL_KMOD_LCTRL 0x40
#define SDL_KMOD_RCTRL 0x80
#define SDL_KMOD_CTRL 0xc0
#define SDL_KMOD_LALT 0x100
#define SDL_KMOD_RALT 0x200
#define SDL_KMOD_ALT 0x300
static inline bool SDL_Init(Uint32 f){(void)f;return true;}
static inline void SDL_Quit(void){}
static inline void SDL_Delay(Uint32 ms){(void)ms;}
static inline Uint64 SDL_GetTicks(void){return 0;}
static inline Uint64 SDL_GetTicksNS(void){return 0;}
static inline const char *SDL_GetError(void){return "headless";}
static inline const char *SDL_GetCurrentVideoDriver(void){return "headless";}
static inline SDL_Window *SDL_CreateWindow(const char *t,int w,int h,Uint64 f)
   {(void)t;(void)w;(void)h;(void)f;return (SDL_Window*)sdl_stub_dummy;}
static inline void SDL_DestroyWindow(SDL_Window *w){(void)w;}
static inline int SDL_GetNumRenderDrivers(void){return 1;}
static inline const char *SDL_GetRenderDriver(int i){(void)i;return "headless";}
static inline SDL_Renderer *SDL_CreateRenderer(SDL_Window *w,const char *n)
   {(void)w;(void)n;return (SDL_Renderer*)sdl_stub_dummy;}
static inline const char *SDL_GetRendererName(SDL_Renderer *r){(void)r;return "headless";}
static inline void SDL_DestroyRenderer(SDL_Renderer *r){(void)r;}
static inline SDL_Texture *SDL_CreateTexture(SDL_Renderer *r,int f,int a,int w,int h)
   {(void)r;(void)f;(void)a;(void)w;(void)h;return (SDL_Texture*)sdl_stub_dummy;}
static inline bool SDL_UpdateTexture(SDL_Texture *t,const void *r,const void *p,int pitch)
   {(void)t;(void)r;(void)p;(void)pitch;return true;}
static inline bool SDL_RenderTexture(SDL_Renderer *r,SDL_Texture *t,const void *s,const void *d)
   {(void)r;(void)t;(void)s;(void)d;return true;}
static inline bool SDL_RenderPresent(SDL_Renderer *r){(void)r;return true;}
static inline bool SDL_SetRenderDrawBlendMode(SDL_Renderer *r,int m){(void)r;(void)m;return true;}
static inline bool SDL_SetTextureBlendMode(SDL_Texture *t,int m){(void)t;(void)m;return true;}
static inline bool SDL_PollEvent(SDL_Event *e){(void)e;return false;}
static inline bool SDL_ShowSimpleMessageBox(Uint32 f,const char *t,const char *m,SDL_Window *w)
   {(void)f;(void)w;fprintf(stderr,"%s: %s\n",t,m);return true;}
#endif
