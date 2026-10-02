/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
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

#if defined(ATARI_NATIVE)
#include <stddef.h>
#include <mint/osbind.h>
#include "atari_check.h"
#include "i_timer.h"

#ifndef PLATFORM_TIMER_HZ
#define PLATFORM_TIMER_HZ 200
#endif

#define TOS_HZ_200_ADDR 0x4BA

static unsigned long basetime = 0;
static unsigned long music_service_hz200 = 0;

extern void MUSIC_Service(void);

/* A copy of _hz_200 the program can read without Super(), which costs two
 * GEMDOS traps and was called dozens of times a frame. Timer C (the
 * interrupt behind _hz_200) bumps it, then carries on into TOS's handler.
 * Hooked while ROTT has the screen: see I_HookTimer. */
volatile unsigned long atari_hz200_count;
void *atari_old_timer_c;
static int timer_hooked;

void atari_timer_c(void);
__asm__(
    "    .text\n"
    "    .even\n"
    "_atari_timer_c:\n"
    "    addq.l  #1,_atari_hz200_count\n"
    "    move.l  _atari_old_timer_c,-(sp)\n"
    "    rts\n");

void I_HookTimer(int on)
{
    long ssp;
    unsigned short sr;

    on = on != 0;
    if (on == timer_hooked)
        return;
    ssp = Super(0L);
    __asm__ volatile("move.w %%sr,%0\n\tori.w #0x0700,%%sr" : "=d"(sr) : : "cc");
    if (on)
    {
        atari_hz200_count = *(volatile unsigned long *)TOS_HZ_200_ADDR;
        atari_old_timer_c = *(void *volatile *)0x114;
        *(void *volatile *)0x114 = (void *)atari_timer_c;
    }
    else
    {
        *(void *volatile *)0x114 = atari_old_timer_c;
    }
    __asm__ volatile("move.w %0,%%sr" : : "d"(sr) : "cc");
    Super(ssp);
    timer_hooked = on;
}

static unsigned long tos_hz200(void)
{
    if (timer_hooked)
        return atari_hz200_count;
    if (Super(1L))
        return *(volatile unsigned long *)TOS_HZ_200_ADDR; /* already super */
    long old = Super(0L);
    volatile unsigned long *hz200 = (volatile unsigned long *)TOS_HZ_200_ADDR;
    unsigned long ticks = *hz200;
    if (old)
        Super(old);
    return ticks;
}

/* ticks * TICRATE / PLATFORM_TIMER_HZ, as I_GetTime always returned, for a
 * count that only grows: whole seconds are carried along and the rest comes
 * from a table, rather than a 32-bit division (a library call) on each of
 * the dozens of calls a frame. */
static unsigned long second_ticks, second_tics;
static unsigned char tics_in_second[PLATFORM_TIMER_HZ];

static int hz200_to_tics(unsigned long ticks)
{
    unsigned long d;

    if (!tics_in_second[PLATFORM_TIMER_HZ - 1])
    {
        int i;
        for (i = 0; i < PLATFORM_TIMER_HZ; i++)
            tics_in_second[i] = (unsigned char)((i * TICRATE) / PLATFORM_TIMER_HZ);
    }
    if (ticks < second_ticks)
    {
        second_ticks = 0;
        second_tics = 0;
    }
    d = ticks - second_ticks;
    if (d >= PLATFORM_TIMER_HZ)
    {
        const unsigned long seconds = d / PLATFORM_TIMER_HZ;
        second_ticks += seconds * PLATFORM_TIMER_HZ;
        second_tics += seconds * TICRATE;
        d -= seconds * PLATFORM_TIMER_HZ;
    }
    return (int)(second_tics + tics_in_second[d]);
}

/* The last answer and the 200 Hz count it was for: see I_GetTime. */
static unsigned long last_hz200 = ~0UL;
static int last_tics;

static int __attribute__((noinline)) get_time(unsigned long ticks)
{
    static int dbg_count = 0;
    const unsigned long hz200 = ticks;
#if defined(ATARI_NATIVE)
#ifndef ATARI_DEBUG
#define ATARI_DEBUG 0
#endif
    if (ATARI_DEBUG && dbg_count < 8)
        Cconws("ROTT: I_GetTime entry\r\n");
#endif
    if (music_service_hz200 == 0)
        music_service_hz200 = ticks;
    while ((ticks - music_service_hz200) >= 4)
    {
        MUSIC_Service();
        music_service_hz200 += 4;
    }
#if defined(ATARI_NATIVE)
    if (ATARI_DEBUG && dbg_count < 8)
        Cconws("ROTT: I_GetTime after hz200\r\n");
#endif
    if (basetime == 0)
        basetime = ticks;
    ticks -= basetime;
#if ATARI_LOGIC_CHECK > 0
    if (atari_check_clock_on)
        return atari_check_clock;
#endif
    {
        int t = hz200_to_tics(ticks);
#if defined(ATARI_NATIVE)
        if (ATARI_DEBUG && dbg_count < 8)
        {
            Cconws("ROTT: I_GetTime done\r\n");
            dbg_count++;
        }
#endif
        last_hz200 = hz200;
        last_tics = t;
        return t;
    }
}

/* Called hundreds of times a second, far more often than the 200 Hz count
 * moves on: the same count gives the same answer, with no more work than
 * this (the rest is out of line, so this path stays small). */
int I_GetTime(void)
{
    const unsigned long ticks = tos_hz200();
#if ATARI_LOGIC_CHECK > 0
    if (atari_check_clock_on)
        return get_time(ticks); /* virtual time: no shortcut */
#endif
    if (ticks == last_hz200)
        return last_tics;
    return get_time(ticks);
}

int I_GetTimeMS(void)
{
    unsigned long ticks = tos_hz200();
    if (basetime == 0)
        basetime = ticks;
    ticks -= basetime;
#if (1000 % PLATFORM_TIMER_HZ) == 0
    return (int)(ticks * (1000UL / PLATFORM_TIMER_HZ));
#else
    return (int)((ticks * 1000UL) / PLATFORM_TIMER_HZ);
#endif
}

void I_Sleep(int ms)
{
    unsigned long start = tos_hz200();
    unsigned long wait = (unsigned long)((ms * PLATFORM_TIMER_HZ) / 1000);
    if (wait == 0)
        wait = 1;
    while ((tos_hz200() - start) < wait) { }
}

void I_WaitVBL(int count)
{
    I_Sleep((count * 1000) / 70);
}

void I_InitTimer(void)
{
    basetime = 0;
    music_service_hz200 = 0;
    last_hz200 = ~0UL;
}

void I_ExitTimer(void)
{
}

#elif defined(ATARI_SDL)
// The SDL builds: TOS's 200 Hz count, read through Super() as needed.

#include <sys/time.h>
#include <unistd.h>
#include <mint/osbind.h>

#include "i_timer.h"

#define ATARI_TIMER_HZ 200UL
#define TOS_HZ_200_ADDR 0x4BA

static unsigned long basetime = 0;

static unsigned long mint_hz200(void)
{
    long old = Super(0L);
    volatile unsigned long *hz200 = (volatile unsigned long *)TOS_HZ_200_ADDR;
    unsigned long ticks = *hz200;

    if (old)
        Super(old);

    return ticks;
}

int I_GetTime(void)
{
    unsigned long ticks = mint_hz200();

    if (basetime == 0)
        basetime = ticks;

    ticks -= basetime;
    return (int)((ticks * TICRATE) / ATARI_TIMER_HZ);
}

int I_GetTimeMS(void)
{
    unsigned long ticks = mint_hz200();

    if (basetime == 0)
        basetime = ticks;

    ticks -= basetime;
    return (int)((ticks * 1000UL) / ATARI_TIMER_HZ);
}

void I_Sleep(int ms)
{
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
}

void I_WaitVBL(int count)
{
    I_Sleep((count * 1000) / 70);
}

void I_InitTimer(void)
{
    basetime = 0;
}

void I_ExitTimer(void)
{
}

#else
// Lantus 3/1/2013 - AmigaOS native

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include <devices/timer.h>
#include <proto/exec.h>
 
#include "i_timer.h"
 
static ULONG basetime = 0;
struct MsgPort *timer_msgport;
struct timerequest *timer_ioreq;
struct Library *TimerBase;

static int opentimer(ULONG unit){
	timer_msgport = CreateMsgPort();
	timer_ioreq = CreateIORequest(timer_msgport, sizeof(*timer_ioreq));
	if (timer_ioreq){
		if (OpenDevice(TIMERNAME, unit, (APTR) timer_ioreq, 0) == 0){
			TimerBase = (APTR) timer_ioreq->tr_node.io_Device;
			return 1;
		}
	}
	return 0;
}
static void closetimer(void){
	if (TimerBase){
		CloseDevice((APTR) timer_ioreq);
	}
	DeleteIORequest(timer_ioreq);
	DeleteMsgPort(timer_msgport);
	TimerBase = 0;
	timer_ioreq = 0;
	timer_msgport = 0;
}

static struct timeval startTime;

void startup(){
	GetSysTime(&startTime);
}

ULONG getMilliseconds(){
	struct timeval endTime;

	GetSysTime(&endTime);
	SubTime(&endTime,&startTime);

	return (endTime.tv_secs * 1000 + endTime.tv_micro / 1000);
}

int  I_GetTime (void)
{
    ULONG ticks;

    ticks = getMilliseconds();

    if (basetime == 0)
        basetime = ticks;

    ticks -= basetime;

    return (ticks * TICRATE) / 1000;
}

//
// Same as I_GetTime, but returns time in milliseconds
//

int I_GetTimeMS(void)
{
    ULONG ticks;

    ticks = getMilliseconds();

    if (basetime == 0)
        basetime = ticks;

    return ticks - basetime;
}

// Sleep for a specified number of ms


void I_ExitTimer()
{
    closetimer();
}

void I_Sleep(int ms)
{
    usleep(ms);
}

void I_WaitVBL(int count)
{
    I_Sleep((count * 1000) / 70);
}


void I_InitTimer(void)
{
    // initialize timer

   opentimer(UNIT_VBLANK);
   startup();
}


#endif
