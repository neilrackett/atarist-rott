// Emacs style mode select   -*- C++ -*- 
//-----------------------------------------------------------------------------
//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 2005 Simon Howard
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
// 02111-1307, USA.
//
// DESCRIPTION:
//      Timer functions.
//
//----------------------------------------------------------------------------- 

#include "i_timer.h"
#include "SDL.h"
#ifdef __EMSCRIPTEN__
extern void emscripten_sleep(unsigned int ms);
#endif

static Uint32 basetime = 0;

int I_GetTime(void)
{
    const Uint32 ticks = SDL_GetTicks() - basetime;
    return (int) ((ticks * TICRATE) / 1000);
}

int I_GetTimeMS(void)
{
    return (int) (SDL_GetTicks() - basetime);
}

void I_Sleep(int ms)
{
    if (ms <= 0)
    {
        return;
    }

#ifdef __EMSCRIPTEN__
    emscripten_sleep(ms);
#else
    SDL_Delay((Uint32) ms);
#endif
}

void I_WaitVBL(int count)
{
    I_Sleep((count * 1000) / 70);
}

void I_InitTimer(void)
{
    if (!SDL_WasInit(SDL_INIT_TIMER))
    {
        if (!SDL_WasInit(0))
        {
            SDL_Init(SDL_INIT_TIMER);
        }
        else
        {
            SDL_InitSubSystem(SDL_INIT_TIMER);
        }
    }

    basetime = SDL_GetTicks();
}
