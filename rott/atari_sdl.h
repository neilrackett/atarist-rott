#ifndef ATARI_SDL_H
#define ATARI_SDL_H

#include "SDL.h"

/*
 * Atari MiNT SDL compatibility helpers.
 * Keeps the ST low-resolution video-mode fallback logic in one place.
 */
int ATARI_SDL_OpenVideo(int width, int height, int *fullscreen,
                        SDL_Surface **video_surface,
                        SDL_Surface **blit_surface);
void ATARI_SDL_CloseVideo(SDL_Surface **video_surface,
                          SDL_Surface **blit_surface);

#endif
