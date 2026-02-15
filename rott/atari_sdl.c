#include <stdio.h>

#include "SDL.h"
#if defined(__MINT__)
#include <mint/osbind.h>
#endif

#include "atari_sdl.h"

#if defined(__MINT__)
#ifndef ATARI_TARGET_FPS
#define ATARI_TARGET_FPS 12
#endif

static char sdl_video_driver_env[48];

static void atari_set_video_driver(const char *driver)
{
    if ((driver == NULL) || (*driver == '\0'))
        return;

    snprintf(sdl_video_driver_env, sizeof(sdl_video_driver_env),
             "SDL_VIDEODRIVER=%s", driver);
    SDL_putenv(sdl_video_driver_env);
}
#endif

static SDL_Surface *atari_try_set_video_mode(int width, int height, Uint32 flags)
{
    /* Prefer paletted 8-bit mode first, then fall back. */
    static const int bpp_try[] = { 8, 0, 4, 16, 32 };
    int i;
    SDL_Surface *surface;

    surface = NULL;
    for (i = 0; i < (int)(sizeof(bpp_try) / sizeof(bpp_try[0])); ++i)
    {
        surface = SDL_SetVideoMode(width, height, bpp_try[i], flags);
        if (surface != NULL)
            break;
    }

    return surface;
}

void ATARI_SDL_CloseVideo(SDL_Surface **video_surface, SDL_Surface **blit_surface)
{
    SDL_Surface *video;
    SDL_Surface *blit;

    video = (video_surface != NULL) ? *video_surface : NULL;
    blit = (blit_surface != NULL) ? *blit_surface : NULL;

    if ((blit != NULL) && (blit != video))
        SDL_FreeSurface(blit);

    if (video != NULL)
        SDL_QuitSubSystem(SDL_INIT_VIDEO);

    if (blit_surface != NULL)
        *blit_surface = NULL;
    if (video_surface != NULL)
        *video_surface = NULL;
}

int ATARI_SDL_OpenVideo(int width, int height, int *fullscreen,
                        SDL_Surface **video_surface,
                        SDL_Surface **blit_surface)
{
    Uint32 flags;
    int want_fullscreen;
    SDL_Surface *video;
    SDL_Surface *blit;
#if defined(__MINT__)
    int prefer_xbios;
    char driver_name[32];
    const char *active_driver;
#endif

    if ((fullscreen == NULL) || (video_surface == NULL) || (blit_surface == NULL))
        return -1;

    want_fullscreen = (*fullscreen != 0);

#if defined(__MINT__)
    prefer_xbios = want_fullscreen;
    atari_set_video_driver(prefer_xbios ? "xbios" : "gem");
#endif

    if (SDL_WasInit(SDL_INIT_VIDEO) == 0)
    {
        if (SDL_Init(SDL_INIT_VIDEO) < 0)
        {
#if defined(__MINT__)
            if (prefer_xbios)
            {
                atari_set_video_driver("gem");
                want_fullscreen = 0;
                if (SDL_Init(SDL_INIT_VIDEO) >= 0)
                    goto sdl_video_inited;
            }
#endif
            return -1;
        }
    }

#if defined(__MINT__)
sdl_video_inited:
    active_driver = SDL_VideoDriverName(driver_name, (int)sizeof(driver_name));
    if (active_driver == NULL)
        active_driver = "unknown";
    printf("SDL video driver: %s\n", active_driver);
    printf("SDL target FPS: %d\n", (int)ATARI_TARGET_FPS);
    printf("SDL video: free before mode = %ld bytes\n", Malloc(-1L));
#endif

    flags = SDL_SWSURFACE;
    if (want_fullscreen)
        flags |= SDL_FULLSCREEN;

    video = atari_try_set_video_mode(width, height, flags);

#if defined(__MINT__)
    if ((video == NULL) && want_fullscreen)
    {
        /* XBIOS failed: retry with GEM/windowed mode. */
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        atari_set_video_driver("gem");
        if (SDL_Init(SDL_INIT_VIDEO) >= 0)
        {
            want_fullscreen = 0;
            flags = SDL_SWSURFACE;
            video = atari_try_set_video_mode(width, height, flags);
        }
    }

    if ((video == NULL) && !want_fullscreen)
    {
        /* GEM failed: retry with XBIOS fullscreen mode. */
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        atari_set_video_driver("xbios");
        if (SDL_Init(SDL_INIT_VIDEO) >= 0)
        {
            want_fullscreen = 1;
            flags = SDL_SWSURFACE | SDL_FULLSCREEN;
            video = atari_try_set_video_mode(width, height, flags);
        }
    }
#else
    if ((video == NULL) && want_fullscreen)
        video = atari_try_set_video_mode(width, height, SDL_SWSURFACE);
#endif

    if (video == NULL)
        return -1;

    blit = video;
    if (video->format->BitsPerPixel != 8)
    {
        blit = SDL_CreateRGBSurface(SDL_SWSURFACE, width, height, 8, 0, 0, 0, 0);
        if (blit == NULL)
        {
            ATARI_SDL_CloseVideo(&video, &blit);
            return -1;
        }
    }

    *fullscreen = want_fullscreen;
    *video_surface = video;
    *blit_surface = blit;
    return 0;
}
