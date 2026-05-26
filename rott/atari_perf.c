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

#include "atari_perf.h"

#if defined(__MINT__)
#include <mint/osbind.h>
#include <stdio.h>

#ifndef ATARI_PROFILE
#define ATARI_PROFILE 0
#endif
#ifndef ATARI_FLAT_WORLD
#define ATARI_FLAT_WORLD 0
#endif
#ifndef ATARI_ACTOR_BUDGET
#define ATARI_ACTOR_BUDGET 0
#endif
#ifndef ATARI_SPRITE_BUDGET
#define ATARI_SPRITE_BUDGET 0
#endif
#ifndef ATARI_EFFECT_BUDGET
#define ATARI_EFFECT_BUDGET 0
#endif

unsigned int atari_actor_budget_runtime = (unsigned int)ATARI_ACTOR_BUDGET;
unsigned int atari_sprite_budget_runtime = (unsigned int)ATARI_SPRITE_BUDGET;
unsigned int atari_effect_budget_runtime = (unsigned int)ATARI_EFFECT_BUDGET;
int atari_flat_world_runtime = (ATARI_FLAT_WORLD != 0);

unsigned int atari_frame_actor_updates = 0;
unsigned int atari_frame_sprite_draws = 0;
unsigned int atari_frame_effect_passes = 0;
int atari_frame_tics = 1;

#if ATARI_PROFILE
static unsigned int atari_profile_frame_count = 0;
static unsigned int atari_profile_actor_sum = 0;
static unsigned int atari_profile_sprite_sum = 0;
static unsigned int atari_profile_effect_sum = 0;
static unsigned int atari_profile_tics_sum = 0;
#endif

void ATARI_PerfBeginFrame(int frame_tics)
{
   atari_frame_tics = (frame_tics <= 0) ? 1 : frame_tics;
   atari_frame_actor_updates = 0;
   atari_frame_sprite_draws = 0;
   atari_frame_effect_passes = 0;
}

void ATARI_PerfEndFrame(void)
{
#if ATARI_PROFILE
   char line[120];
   atari_profile_frame_count++;
   atari_profile_actor_sum += atari_frame_actor_updates;
   atari_profile_sprite_sum += atari_frame_sprite_draws;
   atari_profile_effect_sum += atari_frame_effect_passes;
   atari_profile_tics_sum += (unsigned int)atari_frame_tics;

   if (atari_profile_frame_count >= 32)
   {
      sprintf(line, "ATARI PERF t:%u a:%u s:%u e:%u\r\n",
              atari_profile_tics_sum / atari_profile_frame_count,
              atari_profile_actor_sum / atari_profile_frame_count,
              atari_profile_sprite_sum / atari_profile_frame_count,
              atari_profile_effect_sum / atari_profile_frame_count);
      Cconws(line);

      atari_profile_frame_count = 0;
      atari_profile_actor_sum = 0;
      atari_profile_sprite_sum = 0;
      atari_profile_effect_sum = 0;
      atari_profile_tics_sum = 0;
   }
#endif
}

int ATARI_PerfTryUseEffectPass(void)
{
   if (atari_effect_budget_runtime > 0 &&
       atari_frame_effect_passes >= atari_effect_budget_runtime)
      return 0;

   atari_frame_effect_passes++;
   return 1;
}

#endif
