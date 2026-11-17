/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef ATARI_SDL_H
#define ATARI_SDL_H

#include "SDL.h"

/*
 * The SDL builds' screen. SDL.h is STDL's SDL1 add-on's (lib/stdl): one
 * screen, 320x200 at a byte a pixel, always full screen.
 */
int ATARI_SDL_OpenVideo(int width, int height, int *fullscreen,
                        SDL_Surface **video_surface,
                        SDL_Surface **blit_surface);
void ATARI_SDL_CloseVideo(SDL_Surface **video_surface,
                          SDL_Surface **blit_surface);

#endif
