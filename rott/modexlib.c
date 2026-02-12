/*
Copyright (C) 1994-1995 Apogee Software, Ltd.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "SDL.h"
#ifdef __EMSCRIPTEN__
extern void emscripten_sleep(unsigned int ms);
#endif

#include "rt_def.h"
#include "modexlib.h"
#include "keyb.h"
#include "isr.h"
#include "rt_in.h"
#include "rt_cfg.h"
#include "rt_view.h"
#include "music.h"

boolean StretchScreen = 0;
extern boolean iG_aimCross;
extern boolean sdl_fullscreen;
extern int iG_X_center;
extern int iG_Y_center;
char *iG_buf_center;
int iG_playerTilt = 0;

int linewidth;
int ylookup[600];
byte *page1start;
byte *page2start;
byte *page3start;
int screensize;
byte *bufferofs;
byte *displayofs;
boolean graphicsmode = false;
char *bufofsTopLimit;
char *bufofsBottomLimit;

int mouse_threshold = 10;
int usemouse = 1;
boolean screenvisible = 1;

signed short mx = 0;
signed short my = 0;
word mb = 0;

static SDL_Window *video_window = NULL;
static SDL_Renderer *video_renderer = NULL;
static SDL_Texture *video_texture = NULL;
static SDL_PixelFormat *video_format = NULL;
static byte *video_buffer = NULL;
static Uint32 *video_rgba = NULL;
static SDL_Color current_palette[256];
static Uint32 palette32[256];
static int sdl_initialized = 0;

static int translate_key(SDL_Keycode key)
{
    switch (key)
    {
        case SDLK_ESCAPE: return sc_Escape;
        case SDLK_1: return sc_1;
        case SDLK_2: return sc_2;
        case SDLK_3: return sc_3;
        case SDLK_4: return sc_4;
        case SDLK_5: return sc_5;
        case SDLK_6: return sc_6;
        case SDLK_7: return sc_7;
        case SDLK_8: return sc_8;
        case SDLK_9: return sc_9;
        case SDLK_0: return sc_0;
        case SDLK_MINUS: return sc_Minus;
        case SDLK_EQUALS: return sc_Equals;
        case SDLK_BACKSPACE: return sc_BackSpace;
        case SDLK_TAB: return sc_Tab;
        case SDLK_q: return sc_Q;
        case SDLK_w: return sc_W;
        case SDLK_e: return sc_E;
        case SDLK_r: return sc_R;
        case SDLK_t: return sc_T;
        case SDLK_y: return sc_Y;
        case SDLK_u: return sc_U;
        case SDLK_i: return sc_I;
        case SDLK_o: return sc_O;
        case SDLK_p: return sc_P;
        case SDLK_LEFTBRACKET: return sc_OpenBracket;
        case SDLK_RIGHTBRACKET: return sc_CloseBracket;
        case SDLK_RETURN: return sc_Return;
        case SDLK_KP_ENTER: return sc_Return;
        case SDLK_LCTRL:
        case SDLK_RCTRL: return sc_Control;
        case SDLK_a: return sc_A;
        case SDLK_s: return sc_S;
        case SDLK_d: return sc_D;
        case SDLK_f: return sc_F;
        case SDLK_g: return sc_G;
        case SDLK_h: return sc_H;
        case SDLK_j: return sc_J;
        case SDLK_k: return sc_K;
        case SDLK_l: return sc_L;
        case SDLK_SEMICOLON: return 0x27;
        case SDLK_QUOTE: return 0x28;
        case SDLK_BACKQUOTE: return 0x29;
        case SDLK_LSHIFT:
        case SDLK_RSHIFT: return sc_RShift;
        case SDLK_BACKSLASH: return 0x2b;
        case SDLK_z: return sc_Z;
        case SDLK_x: return sc_X;
        case SDLK_c: return sc_C;
        case SDLK_v: return sc_V;
        case SDLK_b: return sc_B;
        case SDLK_n: return sc_N;
        case SDLK_m: return sc_M;
        case SDLK_COMMA: return sc_Comma;
        case SDLK_PERIOD: return sc_Period;
        case SDLK_SLASH:
        case SDLK_KP_DIVIDE: return 0x35;
        case SDLK_KP_MULTIPLY: return 0x37;
        case SDLK_LALT:
        case SDLK_RALT: return sc_Alt;
        case SDLK_SPACE: return sc_Space;
        case SDLK_CAPSLOCK: return sc_CapsLock;
        case SDLK_F1: return sc_F1;
        case SDLK_F2: return sc_F2;
        case SDLK_F3: return sc_F3;
        case SDLK_F4: return sc_F4;
        case SDLK_F5: return sc_F5;
        case SDLK_F6: return sc_F6;
        case SDLK_F7: return sc_F7;
        case SDLK_F8: return sc_F8;
        case SDLK_F9: return sc_F9;
        case SDLK_F10: return sc_F10;
        case SDLK_F11: return sc_F11;
        case SDLK_F12: return sc_F12;
        case SDLK_NUMLOCKCLEAR: return 0x45;
        case SDLK_SCROLLLOCK: return 0x46;
        case SDLK_KP_7:
        case SDLK_HOME: return sc_Home;
        case SDLK_KP_8:
        case SDLK_UP: return sc_UpArrow;
        case SDLK_KP_9:
        case SDLK_PAGEUP: return sc_PgUp;
        case SDLK_KP_MINUS: return sc_Minus;
        case SDLK_KP_4:
        case SDLK_LEFT: return sc_LeftArrow;
        case SDLK_KP_6:
        case SDLK_RIGHT: return sc_RightArrow;
        case SDLK_KP_PLUS: return sc_Plus;
        case SDLK_KP_1:
        case SDLK_END: return sc_End;
        case SDLK_KP_2:
        case SDLK_DOWN: return sc_DownArrow;
        case SDLK_KP_3:
        case SDLK_PAGEDOWN: return sc_PgDn;
        case SDLK_DELETE:
        case SDLK_KP_PERIOD: return sc_Delete;
        case SDLK_INSERT:
        case SDLK_KP_0: return sc_Insert;
        default: return 0;
    }
}

static void push_key_event(int scancode, int key_up)
{
    int code = scancode;

    if ((code <= 0) || (code >= MAXKEYBOARDSCAN))
    {
        return;
    }

    if (key_up)
    {
        Keystate[code] = 0;
        code |= 0x80;
    }
    else
    {
        Keystate[code] = 1;
        LastScan = code;
    }

    KeyboardQueue[Keytail] = code;
    Keytail = (Keytail + 1) & (KEYQMAX - 1);
}

static void update_palette32(void)
{
    int i;

    for (i = 0; i < 256; i++)
    {
        if (video_format != NULL)
        {
            palette32[i] = SDL_MapRGB(video_format,
                                      current_palette[i].r,
                                      current_palette[i].g,
                                      current_palette[i].b);
        }
        else
        {
            palette32[i] = 0xFF000000u |
                           ((Uint32) current_palette[i].r << 16) |
                           ((Uint32) current_palette[i].g << 8) |
                           (Uint32) current_palette[i].b;
        }
    }
}

static void ensure_video_buffer(void)
{
    const int needed = iGLOBAL_SCREENWIDTH * iGLOBAL_SCREENHEIGHT;

    if ((video_buffer != NULL) && (video_rgba != NULL) && (screensize == needed))
    {
        return;
    }

    free(video_buffer);
    video_buffer = NULL;

    free(video_rgba);
    video_rgba = NULL;

    video_buffer = (byte *) malloc((size_t) needed);
    if (video_buffer == NULL)
    {
        fprintf(stderr, "Out of memory allocating video buffer (%d bytes).\n", needed);
        exit(1);
    }

    video_rgba = (Uint32 *) malloc((size_t) needed * sizeof(Uint32));
    if (video_rgba == NULL)
    {
        fprintf(stderr, "Out of memory allocating display buffer (%d pixels).\n", needed);
        exit(1);
    }

    memset(video_buffer, 0, (size_t) needed);
    memset(video_rgba, 0, (size_t) needed * sizeof(Uint32));
}

static void I_FinishUpdate(void)
{
    int i;
    const int pitch = iGLOBAL_SCREENWIDTH * (int) sizeof(Uint32);

    if ((video_renderer == NULL) || (video_texture == NULL) ||
        (video_buffer == NULL) || (video_rgba == NULL))
    {
        return;
    }

    for (i = 0; i < screensize; i++)
    {
        video_rgba[i] = palette32[video_buffer[i]];
    }

    if (SDL_UpdateTexture(video_texture, NULL, video_rgba, pitch) != 0)
    {
        return;
    }

    SDL_RenderClear(video_renderer);
    SDL_RenderCopy(video_renderer, video_texture, NULL, NULL);
    SDL_RenderPresent(video_renderer);
}

void I_SetPalette(byte *palette)
{
    int i;

    for (i = 0; i < 256; i++)
    {
        current_palette[i].r = (Uint8) (gammatable[(gammaindex << 6) + (*palette++)] << 2);
        current_palette[i].g = (Uint8) (gammatable[(gammaindex << 6) + (*palette++)] << 2);
        current_palette[i].b = (Uint8) (gammatable[(gammaindex << 6) + (*palette++)] << 2);
        current_palette[i].a = SDL_ALPHA_OPAQUE;
    }

    update_palette32();
}

void I_ShutdownGraphics(void)
{
    if (video_texture != NULL)
    {
        SDL_DestroyTexture(video_texture);
        video_texture = NULL;
    }

    if (video_renderer != NULL)
    {
        SDL_DestroyRenderer(video_renderer);
        video_renderer = NULL;
    }

    if (video_window != NULL)
    {
        SDL_DestroyWindow(video_window);
        video_window = NULL;
    }

    if (video_format != NULL)
    {
        SDL_FreeFormat(video_format);
        video_format = NULL;
    }

    free(video_buffer);
    video_buffer = NULL;

    free(video_rgba);
    video_rgba = NULL;

    graphicsmode = false;
}

void GraphicsMode(void)
{
    Uint32 window_flags = SDL_WINDOW_SHOWN;

    if (!sdl_initialized)
    {
        if ((SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) &&
            (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0))
        {
            fprintf(stderr, "SDL video init failed: %s\n", SDL_GetError());
            exit(1);
        }

        sdl_initialized = 1;
    }

    I_ShutdownGraphics();

    if (sdl_fullscreen)
    {
        window_flags |= SDL_WINDOW_FULLSCREEN;
    }

    video_window = SDL_CreateWindow("Rise of the Triad",
                                    SDL_WINDOWPOS_CENTERED,
                                    SDL_WINDOWPOS_CENTERED,
                                    iGLOBAL_SCREENWIDTH,
                                    iGLOBAL_SCREENHEIGHT,
                                    window_flags);
    if ((video_window == NULL) && sdl_fullscreen)
    {
        video_window = SDL_CreateWindow("Rise of the Triad",
                                        SDL_WINDOWPOS_CENTERED,
                                        SDL_WINDOWPOS_CENTERED,
                                        iGLOBAL_SCREENWIDTH,
                                        iGLOBAL_SCREENHEIGHT,
                                        SDL_WINDOW_SHOWN);
    }

    if (video_window == NULL)
    {
        fprintf(stderr, "Failed creating %dx%d window: %s\n",
                iGLOBAL_SCREENWIDTH, iGLOBAL_SCREENHEIGHT, SDL_GetError());
        exit(1);
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");

    video_renderer = SDL_CreateRenderer(video_window, -1,
                                        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (video_renderer == NULL)
    {
        video_renderer = SDL_CreateRenderer(video_window, -1, SDL_RENDERER_SOFTWARE);
    }

    if (video_renderer == NULL)
    {
        fprintf(stderr, "Failed creating renderer: %s\n", SDL_GetError());
        exit(1);
    }

    video_texture = SDL_CreateTexture(video_renderer,
                                      SDL_PIXELFORMAT_ARGB8888,
                                      SDL_TEXTUREACCESS_STREAMING,
                                      iGLOBAL_SCREENWIDTH,
                                      iGLOBAL_SCREENHEIGHT);
    if (video_texture == NULL)
    {
        fprintf(stderr, "Failed creating screen texture: %s\n", SDL_GetError());
        exit(1);
    }

    video_format = SDL_AllocFormat(SDL_PIXELFORMAT_ARGB8888);
    if (video_format == NULL)
    {
        fprintf(stderr, "Failed creating pixel format: %s\n", SDL_GetError());
        exit(1);
    }

    update_palette32();
    SDL_RenderSetLogicalSize(video_renderer, iGLOBAL_SCREENWIDTH, iGLOBAL_SCREENHEIGHT);
    SDL_ShowCursor(SDL_DISABLE);
    SDL_SetRelativeMouseMode(SDL_TRUE);
    SDL_SetWindowGrab(video_window, SDL_TRUE);
    graphicsmode = true;
}

void SetTextMode(void)
{
    I_ShutdownGraphics();
}

void TurnOffTextCursor(void)
{
}

void WaitVBL(void)
{
#ifdef __EMSCRIPTEN__
    emscripten_sleep(1);
#else
    SDL_Delay(1);
#endif
}

void VL_SetVGAPlaneMode(void)
{
    int i;
    int offset;

    GraphicsMode();

    linewidth = iGLOBAL_SCREENWIDTH;
    screensize = iGLOBAL_SCREENWIDTH * iGLOBAL_SCREENHEIGHT;

    ensure_video_buffer();

    offset = 0;
    for (i = 0; i < iGLOBAL_SCREENHEIGHT; i++)
    {
        ylookup[i] = offset;
        offset += linewidth;
    }

    page1start = video_buffer;
    page2start = video_buffer;
    page3start = video_buffer;
    displayofs = page1start;
    bufferofs = page2start;

    iG_X_center = iGLOBAL_SCREENWIDTH / 2;
    iG_Y_center = (iGLOBAL_SCREENHEIGHT / 2) + 10;
    iG_buf_center = (char *) (bufferofs + (screensize / 2));

    bufofsTopLimit = (char *) (bufferofs + screensize - iGLOBAL_SCREENWIDTH);
    bufofsBottomLimit = (char *) (bufferofs + iGLOBAL_SCREENWIDTH);

    XFlipPage();
}

void VL_CopyPlanarPage(byte *src, byte *dest)
{
    memcpy(dest, src, (size_t) screensize);
}

void VL_CopyPlanarPageToMemory(byte *src, byte *dest)
{
    memcpy(dest, src, (size_t) screensize);
}

void VL_CopyBufferToAll(byte *buffer)
{
    memcpy(page1start, buffer, (size_t) screensize);
}

void VL_CopyDisplayToHidden(void)
{
    VL_CopyBufferToAll(displayofs);
}

void VL_ClearBuffer(byte *buf, byte color)
{
    memset(buf, color, (size_t) screensize);
}

void VL_ClearVideo(byte color)
{
    if (video_buffer != NULL)
    {
        memset(video_buffer, color, (size_t) screensize);
    }
}

void VL_DePlaneVGA(void)
{
}

void VH_UpdateScreen(void)
{
    I_FinishUpdate();
}

void XFlipPage(void)
{
    I_FinishUpdate();
#ifdef __EMSCRIPTEN__
    emscripten_sleep(0);
#endif
}

void EnableScreenStretch(void)
{
    StretchScreen = 1;
}

void DisableScreenStretch(void)
{
    StretchScreen = 0;
}

void DrawCenterAim(void)
{
}

void doEvents(void)
{
    SDL_Event event;

    if (!sdl_initialized)
    {
        return;
    }

    while (SDL_PollEvent(&event))
    {
        switch (event.type)
        {
            case SDL_QUIT:
                exit(0);
                break;

            case SDL_KEYDOWN:
            {
                const int sc = translate_key(event.key.keysym.sym);
                if (!event.key.repeat)
                {
                    push_key_event(sc, 0);
#ifdef __EMSCRIPTEN__
                    /* Browser audio often needs a user gesture before music can start. */
                    MUSIC_Continue();
#endif
                }
                break;
            }

            case SDL_KEYUP:
            {
                const int sc = translate_key(event.key.keysym.sym);
                push_key_event(sc, 1);
                break;
            }

            case SDL_MOUSEMOTION:
                mx += (signed short) (event.motion.xrel << 3);
                my += (signed short) (event.motion.yrel << 3);
                break;

            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
            {
                const int down = (event.type == SDL_MOUSEBUTTONDOWN);

                if (event.button.button == SDL_BUTTON_LEFT)
                {
                    if (down) mb |= 1;
                    else mb &= ~1;
                }
                else if (event.button.button == SDL_BUTTON_MIDDLE)
                {
                    if (down) mb |= 2;
                    else mb &= ~2;
                }
                else if (event.button.button == SDL_BUTTON_RIGHT)
                {
                    if (down) mb |= 4;
                    else mb &= ~4;
                }
#ifdef __EMSCRIPTEN__
                if (down)
                {
                    MUSIC_Continue();
                }
#endif
                break;
            }

            default:
                break;
        }
    }
}
