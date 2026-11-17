/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdio.h>

#include "SDL.h"
#include <mint/osbind.h>

#include "atari_sdl.h"

#ifndef ATARI_TARGET_FPS
#define ATARI_TARGET_FPS 12
#endif

/*
 * On STDL's SDL1 add-on there is one screen and one depth, the game's: no
 * driver to choose, no window, and a surface the game writes straight into.
 */

void ATARI_SDL_CloseVideo(SDL_Surface **video_surface, SDL_Surface **blit_surface)
{
    if ((video_surface != NULL) && (*video_surface != NULL))
    {
        /* SDL_Quit is what puts the desktop's screen back, for the text
         * that follows. It also leaves supervisor mode, so the sound's
         * DMA is stopped first: its registers can't be reached after. */
        SDL_CloseAudio();
        SDL_Quit();
    }

    if (blit_surface != NULL)
        *blit_surface = NULL;
    if (video_surface != NULL)
        *video_surface = NULL;
}

int ATARI_SDL_OpenVideo(int width, int height, int *fullscreen,
                        SDL_Surface **video_surface,
                        SDL_Surface **blit_surface)
{
    SDL_Surface *video;
    char driver_name[32];
    const char *active_driver;

    if ((fullscreen == NULL) || (video_surface == NULL) || (blit_surface == NULL))
        return -1;

    if (SDL_Init(SDL_INIT_VIDEO) < 0)
        return -1;

    /* Printed before the mode is set: after it, text lands on the game's screen. */
    active_driver = SDL_VideoDriverName(driver_name, (int)sizeof(driver_name));
    if (active_driver == NULL)
        active_driver = "unknown";
    printf("SDL video driver: %s\n", active_driver);
    printf("SDL target FPS: %d\n", (int)ATARI_TARGET_FPS);
    printf("SDL video: free before mode = %ld bytes\n", Malloc(-1L));

    video = SDL_SetVideoMode(width, height, 8, SDL_SWSURFACE);
    if (video == NULL)
        return -1;

    *fullscreen = 1;
    *video_surface = video;
    *blit_surface = video;
    return 0;
}

#if ATARI_SDL_BENCH > 0
/*
 * SDL_BENCH=n builds, to measure the SDL builds on a machine or in
 * Hatari without a key pressed. The game goes straight into E1A1 (rt_main.c's
 * turbo), where PlayLoop calls this each frame: the player turns on the
 * spot by the clock, a full turn every 7.3 seconds, so every build draws
 * the same views however many of them it manages. After n seconds the
 * frames drawn are added to BENCH.TXT and the game quits.
 */
#include "rt_def.h"
#include "rt_actor.h"
#include "rt_playr.h"
#include "rt_main.h"
#include "rt_net.h"
#include "isr.h"

void ATARI_SDL_BenchFrame(void)
{
    static int start = -1;
    static int frames;
    int tics;
    FILE *f;

    if ((player == NULL) || (controlupdatestarted != 1))
        return;
    if (start < 0)
        start = GetTicCount();
    tics = GetTicCount() - start;
    godmode = true;
    player->angle = (short)((tics * 8) & (FINEANGLES - 1));
    if (tics < ATARI_SDL_BENCH * VBLCOUNTER)
    {
        frames++;
        return;
    }

    f = fopen("BENCH.TXT", "a");
    if (f != NULL)
    {
        long fps100 = ((long)frames * VBLCOUNTER * 100) / tics;

#if defined(__mc68020__) || defined(__mc68030__)
        fprintf(f, "68030: ");
#else
        fprintf(f, "68000: ");
#endif
        fprintf(f, "%d frames in %d tics, %ld.%02ld fps\n", frames, tics,
                fps100 / 100, fps100 % 100);
        fclose(f);
    }
    QuitGame();
}
#endif
