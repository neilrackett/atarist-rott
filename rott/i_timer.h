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
//      System-specific timer interface
//
//-----------------------------------------------------------------------------


#ifndef __I_TIMER__
#define __I_TIMER__

#define TICRATE 35

// Called by D_DoomLoop,
// returns current time in tics.
int I_GetTime (void);
#if defined(ATARI_NATIVE) && !(ATARI_LOGIC_CHECK > 0)
// Most calls, without one: the 200Hz count has not moved since the last
// answer (dozens of calls a frame, at 340 cycles each through GetTicCount).
// atari_time_hz200 is all ones while the count is not ours (i_timer.c).
extern volatile unsigned long atari_hz200_count;
extern unsigned long atari_time_hz200;
extern int atari_time_tics;
#define I_GetTime() \
   ((atari_hz200_count == atari_time_hz200) ? atari_time_tics : (I_GetTime)())
#endif

// returns current time in ms
int I_GetTimeMS (void);

// Pause for a specified number of ms
void I_Sleep(int ms);

// Initialize timer
void I_InitTimer(void);

// Wait for vertical retrace or pause a bit.
void I_WaitVBL(int count);
#if defined(ATARI_NATIVE)
void I_HookTimer(int on);
// The game clock stops between these (they nest): for waits inside play
// the game shouldn't catch up on afterwards (the Accelerator preparing a
// level). I_GetTimeMS goes on.
void I_PauseTime(void);
void I_ResumeTime(void);
#endif

#endif

