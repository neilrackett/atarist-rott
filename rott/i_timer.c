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

#if defined(__MINT__)
#include <stddef.h>
#include <mint/osbind.h>
#include "i_timer.h"

#ifndef PLATFORM_TIMER_HZ
#define PLATFORM_TIMER_HZ 200
#endif

#define TOS_HZ_200_ADDR 0x4BA

static unsigned long basetime = 0;
static unsigned long music_service_hz200 = 0;

extern void MUSIC_Service(void);

static unsigned long tos_hz200(void)
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
    static int dbg_count = 0;
#if defined(__MINT__)
#ifndef ATARI_DEBUG
#define ATARI_DEBUG 0
#endif
    if (ATARI_DEBUG && dbg_count < 8)
        Cconws("ROTT: I_GetTime entry\r\n");
#endif
    unsigned long ticks = tos_hz200();

    if (music_service_hz200 == 0)
        music_service_hz200 = ticks;
    while ((ticks - music_service_hz200) >= 4)
    {
        MUSIC_Service();
        music_service_hz200 += 4;
    }
#if defined(__MINT__)
    if (ATARI_DEBUG && dbg_count < 8)
        Cconws("ROTT: I_GetTime after hz200\r\n");
#endif
    if (basetime == 0)
        basetime = ticks;
    ticks -= basetime;
    {
        int t = (int)((ticks * TICRATE) / PLATFORM_TIMER_HZ);
#if defined(__MINT__)
        if (ATARI_DEBUG && dbg_count < 8)
        {
            Cconws("ROTT: I_GetTime done\r\n");
            dbg_count++;
        }
#endif
        return t;
    }
}

int I_GetTimeMS(void)
{
    unsigned long ticks = tos_hz200();
    if (basetime == 0)
        basetime = ticks;
    ticks -= basetime;
    return (int)((ticks * 1000UL) / PLATFORM_TIMER_HZ);
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
