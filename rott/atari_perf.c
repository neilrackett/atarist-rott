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

#ifndef ATARI_DYNAMIC_QUALITY
#define ATARI_DYNAMIC_QUALITY 0
#endif
#ifndef ATARI_DYNAMIC_QUALITY_MAX
#define ATARI_DYNAMIC_QUALITY_MAX 0
#endif
#ifndef ATARI_DYNAMIC_QUALITY_UP_TICS
#define ATARI_DYNAMIC_QUALITY_UP_TICS 2
#endif
#ifndef ATARI_DYNAMIC_QUALITY_DOWN_FRAMES
#define ATARI_DYNAMIC_QUALITY_DOWN_FRAMES 12
#endif
#ifndef ATARI_PROFILE
#define ATARI_PROFILE 0
#endif
#ifndef ATARI_LOWP
#define ATARI_LOWP 0
#endif
#ifndef ATARI_RAYCAST_STEP
#define ATARI_RAYCAST_STEP 4
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
#ifndef ATARI_FLAT_WORLD
#define ATARI_FLAT_WORLD 0
#endif

unsigned int atari_dynamic_quality_level = 0;
unsigned int atari_raycast_step_runtime = 4;
unsigned int atari_actor_budget_runtime = 0;
unsigned int atari_sprite_budget_runtime = 0;
unsigned int atari_effect_budget_runtime = 0;
int atari_flat_world_runtime = 0;
int atari_lowp_runtime = 0;

unsigned int atari_frame_actor_updates = 0;
unsigned int atari_frame_sprite_draws = 0;
unsigned int atari_frame_effect_passes = 0;
int atari_frame_tics = 1;

static unsigned int atari_quality_stable_frames = 0;
static unsigned int atari_profile_frame_count = 0;
static unsigned int atari_profile_actor_sum = 0;
static unsigned int atari_profile_sprite_sum = 0;
static unsigned int atari_profile_effect_sum = 0;
static unsigned int atari_profile_quality_sum = 0;
static unsigned int atari_profile_tics_sum = 0;

static unsigned int atari_uclamp(unsigned int value, unsigned int lo, unsigned int hi)
{
   if (value < lo)
      return lo;
   if (value > hi)
      return hi;
   return value;
}

static unsigned int atari_apply_budget(unsigned int base_budget, unsigned int level)
{
   unsigned int budget;

   if (base_budget == 0)
      return 0;

   budget = base_budget - ((base_budget * level) >> 2);
   if (budget < 16)
      budget = 16;
   return budget;
}

static void atari_apply_quality_level(void)
{
   unsigned int level = atari_dynamic_quality_level;
   unsigned int base_step = (ATARI_RAYCAST_STEP > 1) ? (unsigned int)ATARI_RAYCAST_STEP : 4U;
   unsigned int effect_budget = (ATARI_EFFECT_BUDGET > 0) ? (unsigned int)ATARI_EFFECT_BUDGET : 0U;

   if (base_step < 4)
      base_step = 4;

   atari_raycast_step_runtime = base_step + (level << 2);
   atari_raycast_step_runtime = atari_uclamp(atari_raycast_step_runtime, 4, 40);

   atari_actor_budget_runtime = atari_apply_budget((ATARI_ACTOR_BUDGET > 0) ? (unsigned int)ATARI_ACTOR_BUDGET : 0U, level);
   atari_sprite_budget_runtime = atari_apply_budget((ATARI_SPRITE_BUDGET > 0) ? (unsigned int)ATARI_SPRITE_BUDGET : 0U, level);

   if (effect_budget == 0)
      atari_effect_budget_runtime = 0;
   else if (effect_budget > level)
      atari_effect_budget_runtime = effect_budget - level;
   else
      atari_effect_budget_runtime = 1;

   atari_flat_world_runtime = (ATARI_FLAT_WORLD != 0) || (level >= 3);
   atari_lowp_runtime = (ATARI_LOWP != 0) || (level >= 1);
}

static void atari_adjust_dynamic_quality(int frame_tics)
{
   unsigned int max_level = (ATARI_DYNAMIC_QUALITY_MAX > 0) ? (unsigned int)ATARI_DYNAMIC_QUALITY_MAX : 0U;

   if (!ATARI_DYNAMIC_QUALITY || max_level == 0)
   {
      atari_dynamic_quality_level = 0;
      atari_quality_stable_frames = 0;
      return;
   }

   if (frame_tics >= ATARI_DYNAMIC_QUALITY_UP_TICS)
   {
      if (atari_dynamic_quality_level < max_level)
         atari_dynamic_quality_level++;
      atari_quality_stable_frames = 0;
   }
   else
   {
      atari_quality_stable_frames++;
      if (atari_dynamic_quality_level > 0 &&
          atari_quality_stable_frames >= (unsigned int)ATARI_DYNAMIC_QUALITY_DOWN_FRAMES)
      {
         atari_dynamic_quality_level--;
         atari_quality_stable_frames = 0;
      }
   }
}

void ATARI_PerfBeginFrame(int frame_tics)
{
   if (frame_tics <= 0)
      frame_tics = 1;

   atari_frame_tics = frame_tics;
   atari_frame_actor_updates = 0;
   atari_frame_sprite_draws = 0;
   atari_frame_effect_passes = 0;

   atari_adjust_dynamic_quality(frame_tics);
   atari_apply_quality_level();
}

void ATARI_PerfEndFrame(void)
{
#if ATARI_PROFILE
   char line[120];
   atari_profile_frame_count++;
   atari_profile_actor_sum += atari_frame_actor_updates;
   atari_profile_sprite_sum += atari_frame_sprite_draws;
   atari_profile_effect_sum += atari_frame_effect_passes;
   atari_profile_quality_sum += atari_dynamic_quality_level;
   atari_profile_tics_sum += (unsigned int)atari_frame_tics;

   if (atari_profile_frame_count >= 32)
   {
      sprintf(line, "ATARI PERF t:%u q:%u a:%u s:%u e:%u\r\n",
              atari_profile_tics_sum / atari_profile_frame_count,
              atari_profile_quality_sum / atari_profile_frame_count,
              atari_profile_actor_sum / atari_profile_frame_count,
              atari_profile_sprite_sum / atari_profile_frame_count,
              atari_profile_effect_sum / atari_profile_frame_count);
      Cconws(line);

      atari_profile_frame_count = 0;
      atari_profile_actor_sum = 0;
      atari_profile_sprite_sum = 0;
      atari_profile_effect_sum = 0;
      atari_profile_quality_sum = 0;
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
