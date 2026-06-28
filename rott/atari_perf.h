/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
/*
Copyright (C) 1994-1995 Apogee Software, Ltd.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
#ifndef _atari_perf_public
#define _atari_perf_public

#include "rt_def.h"

#if defined(__MINT__)
extern unsigned int atari_actor_budget_runtime;
extern unsigned int atari_sprite_budget_runtime;
extern unsigned int atari_effect_budget_runtime;
extern int atari_flat_world_runtime;

extern unsigned int atari_frame_actor_updates;
extern unsigned int atari_frame_sprite_draws;
extern unsigned int atari_frame_effect_passes;
extern int atari_frame_tics;

void ATARI_PerfBeginFrame(int frame_tics);
void ATARI_PerfEndFrame(void);
int ATARI_PerfTryUseEffectPass(void);
#else
#define ATARI_PerfBeginFrame(frame_tics) ((void)0)
#define ATARI_PerfEndFrame() ((void)0)
#define ATARI_PerfTryUseEffectPass() (1)
#endif

#endif
