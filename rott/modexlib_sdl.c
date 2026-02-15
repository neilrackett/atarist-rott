/*
Copyright (C) 1994-1995 Apogee Software, Ltd.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "SDL.h"

#include "modexlib.h"
#include "atari_sdl.h"
#include "keyb.h"
#include "isr.h"
#include "rt_in.h"
#include "rt_view.h"

boolean StretchScreen = 0;

int linewidth;
int ylookup[600];
byte *page1start;
byte *page2start;
byte *page3start;
int screensize;
byte *bufferofs;
byte *displayofs;
boolean graphicsmode = false;

signed short mx, my;
word mb = 0;

int iG_playerTilt;
boolean screenvisible = 1;

static SDL_Surface *video_surface;
static SDL_Surface *blit_surface;
static byte *screenpixels;
static int video_initialized;

extern int iG_X_center;
extern int iG_Y_center;
extern boolean sdl_fullscreen;

#ifndef ATARI_SHOW_FPS
#define ATARI_SHOW_FPS 0
#endif

#if defined(__MINT__)
#ifndef ATARI_TARGET_FPS
#define ATARI_TARGET_FPS 12
#endif

static int mint_frame_interval = 0;
static int mint_frame_accum = 0;
static int mint_frame_last_tic = -1;
static int mint_render_granted = 0;
static int mint_force_render = 0;

static int mint_compute_frame(void)
{
#if (ATARI_TARGET_FPS <= 0) || (ATARI_TARGET_FPS >= VBLCOUNTER)
    return 1;
#else
    int now = GetTicCount();
    int delta;

    if (mint_frame_interval == 0)
    {
        mint_frame_interval = (VBLCOUNTER << 16) / ATARI_TARGET_FPS;
        if (mint_frame_interval <= 0)
            mint_frame_interval = 1 << 16;
    }

    if (mint_frame_last_tic < 0)
    {
        mint_frame_last_tic = now;
        mint_frame_accum = mint_frame_interval;
        return 1;
    }

    delta = now - mint_frame_last_tic;
    if (delta < 0)
    {
        mint_frame_last_tic = now;
        mint_frame_accum = mint_frame_interval;
        return 1;
    }

    mint_frame_last_tic = now;
    mint_frame_accum += (delta << 16);

    if (mint_frame_accum > (mint_frame_interval * 4))
        mint_frame_accum = mint_frame_interval * 4;

    if (mint_frame_accum >= mint_frame_interval)
    {
        mint_frame_accum -= mint_frame_interval;
        return 1;
    }

    return 0;
#endif
}

int ATARI_BeginRenderFrame(void)
{
    if (mint_render_granted)
        return 1;
    if (mint_force_render)
    {
        mint_force_render = 0;
        mint_render_granted = 1;
        return 1;
    }
    if (mint_compute_frame())
    {
        mint_render_granted = 1;
        return 1;
    }
    return 0;
}

void ATARI_EndRenderFrame(void)
{
    mint_render_granted = 0;
}

void ATARI_ForceRender(void)
{
    mint_force_render = 1;
}
#else
int ATARI_BeginRenderFrame(void)
{
    return 1;
}

void ATARI_EndRenderFrame(void)
{
}

void ATARI_ForceRender(void)
{
}
#endif

#if ATARI_SHOW_FPS
static const byte fps_font_digits[10][5] =
{
    { 0x7, 0x5, 0x5, 0x5, 0x7 }, /* 0 */
    { 0x2, 0x6, 0x2, 0x2, 0x7 }, /* 1 */
    { 0x7, 0x1, 0x7, 0x4, 0x7 }, /* 2 */
    { 0x7, 0x1, 0x7, 0x1, 0x7 }, /* 3 */
    { 0x5, 0x5, 0x7, 0x1, 0x1 }, /* 4 */
    { 0x7, 0x4, 0x7, 0x1, 0x7 }, /* 5 */
    { 0x7, 0x4, 0x7, 0x5, 0x7 }, /* 6 */
    { 0x7, 0x1, 0x1, 0x1, 0x1 }, /* 7 */
    { 0x7, 0x5, 0x7, 0x5, 0x7 }, /* 8 */
    { 0x7, 0x5, 0x7, 0x1, 0x7 }  /* 9 */
};

static const byte fps_font_f[5]  = { 0x7, 0x4, 0x7, 0x4, 0x4 };
static const byte fps_font_p[5]  = { 0x7, 0x5, 0x7, 0x4, 0x4 };
static const byte fps_font_s[5]  = { 0x7, 0x4, 0x7, 0x1, 0x7 };
static const byte fps_font_colon[5] = { 0x0, 0x2, 0x0, 0x2, 0x0 };
static const byte fps_font_blank[5] = { 0x0, 0x0, 0x0, 0x0, 0x0 };

static int fps_last_tic = -1;
static int fps_frames = 0;
static int fps_value = 0;

static const byte *fps_glyph_for_char(char c)
{
    if ((c >= '0') && (c <= '9'))
        return fps_font_digits[(int)(c - '0')];

    switch (c)
    {
        case 'F': return fps_font_f;
        case 'P': return fps_font_p;
        case 'S': return fps_font_s;
        case ':': return fps_font_colon;
        default: return fps_font_blank;
    }
}

static void draw_fps_text(SDL_Surface *target, int x, int y, const char *text, byte fg, byte bg)
{
    int i;
    int row;
    int len;
    int text_width;
    byte *row_dst;
    const byte *glyph;

    if ((target == NULL) || (text == NULL) || (target->pixels == NULL))
        return;

    len = (int)strlen(text);
    if (len <= 0)
        return;

    text_width = len * 4;
    if ((x < 0) || (y < 0) || ((x + text_width) > target->w) || ((y + 5) > target->h))
        return;

    for (row = 0; row < 5; ++row)
    {
        row_dst = (byte *)target->pixels + (size_t)(y + row) * (size_t)target->pitch + x;
        memset(row_dst, bg, (size_t)text_width);
    }

    for (i = 0; i < len; ++i)
    {
        glyph = fps_glyph_for_char(text[i]);
        for (row = 0; row < 5; ++row)
        {
            byte bits = glyph[row];
            byte *px = (byte *)target->pixels + (size_t)(y + row) * (size_t)target->pitch + x + (i * 4);

            if (bits & 0x4) px[0] = fg;
            if (bits & 0x2) px[1] = fg;
            if (bits & 0x1) px[2] = fg;
        }
    }
}

static void draw_fps_overlay(SDL_Surface *target)
{
    char text[12];
    int now;
    int elapsed;
    int x;
    int y;

    if ((target == NULL) || (target->format == NULL) || (target->format->BitsPerPixel != 8))
        return;

    now = GetTicCount();
    if ((fps_last_tic < 0) || (now < fps_last_tic))
    {
        fps_last_tic = now;
        fps_frames = 0;
        fps_value = 0;
    }

    fps_frames++;
    elapsed = now - fps_last_tic;
    if (elapsed >= VBLCOUNTER)
    {
        if (elapsed > 0)
            fps_value = (fps_frames * VBLCOUNTER + (elapsed / 2)) / elapsed;
        else
            fps_value = 0;

        fps_frames = 0;
        fps_last_tic = now;
    }

    if (fps_value < 0)
        fps_value = 0;
    else if (fps_value > 999)
        fps_value = 999;

    snprintf(text, sizeof(text), "FPS:%3d", fps_value);
    x = target->w - ((int)strlen(text) * 4) - 2;
    y = target->h - 7;
    draw_fps_text(target, x, y, text, 255, 0);
}
#endif

static int map_key_to_scancode(SDLKey key)
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

        /* Keep original behavior: map left shift to right shift. */
        case SDLK_LSHIFT:
        case SDLK_RSHIFT: return sc_RShift;

        case SDLK_BACKSLASH:
        case SDLK_WORLD_63: return 0x2B;

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

        case SDLK_LALT:
        case SDLK_RALT:
        case SDLK_MODE: return sc_Alt;

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

        case SDLK_NUMLOCK: return 0x45;
        case SDLK_SCROLLOCK: return 0x46;

        case SDLK_KP7:
        case SDLK_HOME: return sc_Home;
        case SDLK_KP8:
        case SDLK_UP: return sc_UpArrow;
        case SDLK_KP9:
        case SDLK_PAGEUP: return sc_PgUp;

        case SDLK_KP_MINUS: return sc_Minus;
        case SDLK_KP4:
        case SDLK_LEFT: return sc_LeftArrow;
        case SDLK_KP5: return 0x4C;
        case SDLK_KP6:
        case SDLK_RIGHT: return sc_RightArrow;

        case SDLK_KP_PLUS: return sc_Plus;
        case SDLK_KP1:
        case SDLK_END: return sc_End;
        case SDLK_KP2:
        case SDLK_DOWN: return sc_DownArrow;
        case SDLK_KP3:
        case SDLK_PAGEDOWN: return sc_PgDn;
        case SDLK_DELETE: return sc_Delete;
        case SDLK_KP0:
        case SDLK_INSERT: return sc_Insert;
        case SDLK_KP_ENTER: return sc_Return;

        default: return sc_None;
    }
}

static void enqueue_key(int scancode, int pressed)
{
    int next;

    if (scancode == sc_None)
        return;

    next = (Keytail + 1) & (KEYQMAX - 1);
    if (next == Keyhead)
        return;

    if (pressed)
    {
        Keystate[scancode & 0x7F] = 1;
        LastScan = scancode;
        KeyboardQueue[Keytail] = scancode;
    }
    else
    {
        Keystate[scancode & 0x7F] = 0;
        KeyboardQueue[Keytail] = scancode | 0x80;
    }

    Keytail = next;
}

void I_SetPalette(byte *palette)
{
    SDL_Color colors[256];
    int i;
    SDL_Surface *pal_surface = blit_surface != NULL ? blit_surface : video_surface;

    if (pal_surface == NULL)
        return;

    for (i = 0; i < 256; ++i)
    {
        byte r = gammatable[(gammaindex << 6) + (*palette++)];
        byte g = gammatable[(gammaindex << 6) + (*palette++)];
        byte b = gammatable[(gammaindex << 6) + (*palette++)];

        colors[i].r = (Uint8)((r << 2) | (r >> 4));
        colors[i].g = (Uint8)((g << 2) | (g >> 4));
        colors[i].b = (Uint8)((b << 2) | (b >> 4));
    }

    SDL_SetColors(pal_surface, colors, 0, 256);
}

void TurnOffTextCursor(void)
{
}

void WaitVBL(void)
{
    SDL_Delay(1);
}

void GraphicsMode(void)
{
    int want_fullscreen = 0;

    if (video_initialized)
        return;

    want_fullscreen = (sdl_fullscreen != 0);
    if (ATARI_SDL_OpenVideo(iGLOBAL_SCREENWIDTH, iGLOBAL_SCREENHEIGHT,
                            &want_fullscreen, &video_surface, &blit_surface) < 0)
    {
        Error("SDL video setup failed: %s", SDL_GetError());
    }
    sdl_fullscreen = want_fullscreen;

    SDL_WM_SetCaption("Rise of the Triad", "ROTT");
    SDL_ShowCursor(SDL_DISABLE);

    screenpixels = (byte *)malloc((size_t)iGLOBAL_SCREENWIDTH * (size_t)iGLOBAL_SCREENHEIGHT);
    if (screenpixels == NULL)
        Error("Failed to allocate %dx%d framebuffer", iGLOBAL_SCREENWIDTH, iGLOBAL_SCREENHEIGHT);

    memset(screenpixels, 0, (size_t)iGLOBAL_SCREENWIDTH * (size_t)iGLOBAL_SCREENHEIGHT);

    mx = my = 0;
    mb = 0;
    video_initialized = 1;
    graphicsmode = true;

#if defined(__MINT__)
    mint_frame_interval = 0;
    mint_frame_accum = 0;
    mint_frame_last_tic = -1;
    mint_render_granted = 0;
    mint_force_render = 1;
#endif

#if ATARI_SHOW_FPS
    fps_last_tic = -1;
    fps_frames = 0;
    fps_value = 0;
#endif
}

void SetTextMode(void)
{
    if (screenpixels != NULL)
    {
        free(screenpixels);
        screenpixels = NULL;
    }

    if (video_initialized)
    {
        ATARI_SDL_CloseVideo(&video_surface, &blit_surface);
        video_initialized = 0;
    }

#if defined(__MINT__)
    mint_render_granted = 0;
    mint_force_render = 0;
#endif

#if ATARI_SHOW_FPS
    fps_last_tic = -1;
    fps_frames = 0;
    fps_value = 0;
#endif

    graphicsmode = false;
}

void VL_SetVGAPlaneMode(void)
{
    int i;
    int offset;

    GraphicsMode();

    linewidth = iGLOBAL_SCREENWIDTH;
    offset = 0;

    for (i = 0; i < iGLOBAL_SCREENHEIGHT; ++i)
    {
        ylookup[i] = offset;
        offset += linewidth;
    }

    screensize = iGLOBAL_SCREENHEIGHT * iGLOBAL_SCREENWIDTH;

    page1start = screenpixels;
    page2start = screenpixels;
    page3start = screenpixels;
    displayofs = page1start;
    bufferofs = page2start;

    iG_X_center = iGLOBAL_SCREENWIDTH / 2;
    iG_Y_center = (iGLOBAL_SCREENHEIGHT / 2) + 10;

    XFlipPage();
}

void VL_CopyPlanarPage(byte *src, byte *dest)
{
    memcpy(dest, src, (size_t)screensize);
}

void VL_CopyPlanarPageToMemory(byte *src, byte *dest)
{
    memcpy(dest, src, (size_t)screensize);
}

void VL_CopyBufferToAll(byte *buffer)
{
    memcpy(screenpixels, buffer, (size_t)screensize);
}

void VL_CopyDisplayToHidden(void)
{
    VL_CopyBufferToAll(displayofs);
}

void VL_ClearBuffer(byte *buf, byte color)
{
    memset(buf, color, (size_t)screensize);
}

void VL_ClearVideo(byte color)
{
    memset(screenpixels, color, (size_t)iGLOBAL_SCREENWIDTH * (size_t)iGLOBAL_SCREENHEIGHT);
}

void VL_DePlaneVGA(void)
{
}

void I_FinishUpdate(void)
{
    int y;
    byte *src;
    byte *dst;
    SDL_Surface *target;

    if ((video_surface == NULL) || (blit_surface == NULL) || (screenpixels == NULL))
        return;

#if defined(__MINT__)
    if (!mint_render_granted && !ATARI_BeginRenderFrame())
        return;
#endif

    target = blit_surface;

    if (SDL_MUSTLOCK(target))
    {
        if (SDL_LockSurface(target) < 0)
        {
#if defined(__MINT__)
            ATARI_EndRenderFrame();
#endif
            return;
        }
    }

    src = screenpixels;
    dst = (byte *)target->pixels;
    if (target->pitch == iGLOBAL_SCREENWIDTH)
    {
        memcpy(dst, src, (size_t)iGLOBAL_SCREENWIDTH * (size_t)iGLOBAL_SCREENHEIGHT);
    }
    else
    {
        for (y = 0; y < iGLOBAL_SCREENHEIGHT; ++y)
        {
            memcpy(dst, src, (size_t)iGLOBAL_SCREENWIDTH);
            src += iGLOBAL_SCREENWIDTH;
            dst += target->pitch;
        }
    }

#if ATARI_SHOW_FPS
    draw_fps_overlay(target);
#endif

    if (SDL_MUSTLOCK(target))
        SDL_UnlockSurface(target);

    if (blit_surface != video_surface)
        SDL_BlitSurface(blit_surface, NULL, video_surface, NULL);

    SDL_Flip(video_surface);

#if defined(__MINT__)
    ATARI_EndRenderFrame();
#endif
}

void VH_UpdateScreen(void)
{
    I_FinishUpdate();
}

void XFlipPage(void)
{
    I_FinishUpdate();
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

    while (SDL_PollEvent(&event))
    {
        switch (event.type)
        {
            case SDL_QUIT:
                enqueue_key(sc_Escape, 1);
                break;

            case SDL_KEYDOWN:
                enqueue_key(map_key_to_scancode(event.key.keysym.sym), 1);
                break;

            case SDL_KEYUP:
                enqueue_key(map_key_to_scancode(event.key.keysym.sym), 0);
                break;

            case SDL_MOUSEMOTION:
                mx += (signed short)(event.motion.xrel << 2);
                my += (signed short)(event.motion.yrel << 2);
                break;

            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
            {
                int pressed = (event.type == SDL_MOUSEBUTTONDOWN);
                int mask = 0;

                if (event.button.button == SDL_BUTTON_LEFT)
                    mask = 1;
                else if (event.button.button == SDL_BUTTON_MIDDLE)
                    mask = 2;
                else if (event.button.button == SDL_BUTTON_RIGHT)
                    mask = 4;

                if (mask != 0)
                {
                    if (pressed)
                        mb |= mask;
                    else
                        mb &= ~mask;
                }
                break;
            }

            default:
                break;
        }
    }
}
