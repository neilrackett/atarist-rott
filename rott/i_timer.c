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

#include <sys/time.h>
#include <unistd.h>

#if defined(__MINT__)
#include <mint/osbind.h>
#endif

#include "i_timer.h"

static unsigned long basetime = 0;

#if defined(__MINT__)
#define ATARI_TIMER_HZ 200UL
#define TOS_HZ_200_ADDR 0x4BA

static unsigned long mint_hz200(void)
{
    long old = Super(0L);
    volatile unsigned long *hz200 = (volatile unsigned long *)TOS_HZ_200_ADDR;
    unsigned long ticks = *hz200;

    if (old)
        Super(old);

    return ticks;
}

#else

static unsigned long now_ms(void)
{
    struct timeval tv;

    gettimeofday(&tv, NULL);
    return ((unsigned long)tv.tv_sec * 1000UL) + ((unsigned long)tv.tv_usec / 1000UL);
}
#endif

int I_GetTime(void)
{
#if defined(__MINT__)
    unsigned long ticks = mint_hz200();

    if (basetime == 0)
        basetime = ticks;

    ticks -= basetime;
    return (int)((ticks * TICRATE) / ATARI_TIMER_HZ);
#else
    unsigned long ticks = now_ms();

    if (basetime == 0)
        basetime = ticks;

    ticks -= basetime;
    return (int)((ticks * TICRATE) / 1000UL);
#endif
}

int I_GetTimeMS(void)
{
#if defined(__MINT__)
    unsigned long ticks = mint_hz200();

    if (basetime == 0)
        basetime = ticks;

    ticks -= basetime;
    return (int)((ticks * 1000UL) / ATARI_TIMER_HZ);
#else
    unsigned long ticks = now_ms();

    if (basetime == 0)
        basetime = ticks;

    return (int)(ticks - basetime);
#endif
}

void I_Sleep(int ms)
{
#if defined(__MINT__)
    unsigned long start;
    unsigned long wait;

    if (ms <= 0)
        return;

    wait = ((unsigned long)ms * ATARI_TIMER_HZ + 999UL) / 1000UL;
    if (wait == 0)
        wait = 1;

    start = mint_hz200();
    while ((mint_hz200() - start) < wait)
    {
        /* Yield without busy-spinning while waiting for the next tick. */
        usleep(1000UL);
    }
#else
    if (ms > 0)
        usleep((unsigned long)ms * 1000UL);
#endif
}

void I_WaitVBL(int count)
{
    I_Sleep((count * 1000) / 70);
}

void I_InitTimer(void)
{
    basetime = 0;
}
