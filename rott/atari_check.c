/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
/*
 * atari_check.c - ATARI_LOGIC_CHECK=n test builds; see atari_check.h.
 *
 * The game starts as ATARI_MD_AUTOTEST's does. From PlayLoop on, GetTicCount
 * is a virtual clock that CalcTics moves on n tics a frame, so every frame
 * runs the same tics whatever the frame rate; the player is in god mode
 * (nothing ends the run early) and follows a fixed script of controls. The
 * random number tables start at 0 (rt_rand.c). After each frame's game
 * logic, a hash of the actors, statics, doors, walls and player goes to
 * C:\LOGIC.TXT, a line a frame (tic; player, actors, statics and world
 * hashes; actor and static counts) for ATARI_LOGIC_CHECK_TICS
 * tics. Compare two builds' files to see whether a change to the game
 * logic changed what the game does.
 */

#include "atari_check.h"

#if defined(__MINT__) && (ATARI_LOGIC_CHECK > 0)

#include "rt_def.h"

#include <mint/osbind.h>
#include <stdio.h>
#include <string.h>

#include "i_timer.h"
#include "isr.h"
#include "rt_actor.h"
#include "rt_door.h"
#include "rt_main.h"
#include "rt_net.h"
#include "rt_playr.h"
#include "rt_rand.h"
#include "rt_stat.h"
#include "states.h"

#ifndef ATARI_LOGIC_CHECK_TICS
#define ATARI_LOGIC_CHECK_TICS 2100
#endif
/* 0: the same game with no hashing, as a benchmark (frames per second
 * then depend only on how fast the build is). */
#ifndef ATARI_LOGIC_CHECK_HASH
#define ATARI_LOGIC_CHECK_HASH 1
#endif

int atari_check_clock_on;
int atari_check_clock;

/* Lists are walked at most this far, so one that ends in a loop cannot
 * hang the check; the counts go in the hash and on the line. */
#define CHECK_MAX_LIST 4096

/* Set by whichever renderer from what it last drew (the Multi-device's view
 * may be a frame behind, depending on timing), read only by renderers and
 * the automap: not game state. */
#define RENDER_FLAGS (FL_VISIBLE | FL_SEEN)

static int check_start;
static long check_file = -1;
static unsigned long check_hash;

void ATARI_CheckStart(void) {
  if (atari_check_clock_on) return;
  atari_check_clock = I_GetTime();
  atari_check_clock_on = 1;
  check_start = atari_check_clock;
  godmode = 1;
  check_file = Fcreate("LOGIC.TXT", 0);
  /* The controls may have started already, stamped with real time; PlayLoop
   * starts them again next, on the virtual clock. */
  ShutdownClientControls();
}

void ATARI_CheckAdvance(void) {
  if (atari_check_clock_on) atari_check_clock += ATARI_LOGIC_CHECK;
}

/* A 700-tic loop of walking, turning, strafing, running, shooting and
 * opening doors. */
void ATARI_CheckKeys(void) {
  static const struct {
    short until;
    signed char buttons[3];
  } script[] = {
      {140, {di_north, -1, -1}},       {175, {di_east, -1, -1}},
      {315, {di_north, bt_attack, -1}}, {350, {di_west, -1, -1}},
      {360, {bt_use, -1, -1}},          {460, {di_north, bt_run, -1}},
      {500, {di_south, -1, -1}},        {560, {bt_strafe, di_east, -1}},
      {630, {di_north, bt_attack, bt_use}}, {700, {di_west, bt_run, -1}},
  };
  static const signed char used[] = {di_north, di_east, di_south, di_west,
                                     bt_attack, bt_use, bt_run, bt_strafe};
  int t, i, j;

  if (!atari_check_clock_on) return;
  for (i = 0; i < (int)sizeof(used); i++) Keystate[buttonscan[(int)used[i]]] = 0;
  t = (atari_check_clock - check_start) % 700;
  for (i = 0; script[i].until <= t; i++) {
  }
  for (j = 0; j < 3; j++)
    if (script[i].buttons[j] >= 0) Keystate[buttonscan[(int)script[i].buttons[j]]] = 1;
}

static void mix(unsigned long v) { check_hash = (check_hash ^ v) * 16777619UL; }

static void mix_state(const statetype *s) {
  if (!s) {
    mix(0xFFFFFFFFUL);
    return;
  }
  mix(s->rotate);
  mix((unsigned long)s->shapenum);
  mix((unsigned long)s->tictime);
  mix((unsigned long)s->condition);
}

void ATARI_CheckFrame(void) {
  char line[160];
  unsigned long part[4];
  objtype *ob;
  statobj_t *st;
  int i, n, na, tic;

  if (!ATARI_LOGIC_CHECK_HASH || !atari_check_clock_on || check_file < 0) return;
  tic = atari_check_clock - check_start;
  if (tic > ATARI_LOGIC_CHECK_TICS) {
    Fclose((short)check_file);
    check_file = -1;
    return;
  }

  /* One hash per part, so a difference says where to look. */
  check_hash = 2166136261UL;
  mix((unsigned long)gamestate.TimeCount);
  mix((unsigned long)GetRNGindex());
  mix((unsigned long)locplayerstate->health);
  mix((unsigned long)locplayerstate->ammo);
  mix((unsigned long)locplayerstate->weapon);
  mix((unsigned long)locplayerstate->keys);
  mix((unsigned long)gamestate.score);
  mix((unsigned long)player->x);
  mix((unsigned long)player->y);
  mix((unsigned long)player->angle);
  mix((unsigned long)player->momentumx);
  mix((unsigned long)player->momentumy);
  part[0] = check_hash;
  check_hash = 2166136261UL;
  for (ob = FIRSTACTOR, n = 0; ob && n < CHECK_MAX_LIST; ob = ob->next, n++) {
    mix(ob->obclass);
    mix((unsigned long)ob->x);
    mix((unsigned long)ob->y);
    mix((unsigned long)ob->z);
    mix((unsigned long)ob->angle);
    mix((unsigned long)ob->yzangle);
    mix(ob->dir);
    mix(ob->flags & ~RENDER_FLAGS);
    mix((unsigned long)ob->hitpoints);
    mix((unsigned long)ob->ticcount);
    mix((unsigned long)ob->shapenum);
    mix((unsigned long)ob->momentumx);
    mix((unsigned long)ob->momentumy);
    mix((unsigned long)ob->momentumz);
    mix((unsigned long)ob->speed);
    mix((unsigned long)ob->temp1);
    mix((unsigned long)ob->temp2);
    mix((unsigned long)ob->temp3);
    mix((unsigned long)ob->targettilex);
    mix((unsigned long)ob->targettiley);
    mix((unsigned long)ob->dirchoosetime);
    mix_state(ob->state);
  }
  mix((unsigned long)n);
  na = n;
  part[1] = check_hash;
  check_hash = 2166136261UL;
  for (st = FIRSTSTAT, n = 0; st && n < CHECK_MAX_LIST; st = st->statnext, n++) {
    mix((unsigned long)st->x);
    mix((unsigned long)st->y);
    mix((unsigned long)st->z);
    mix(st->flags & ~RENDER_FLAGS);
    mix((unsigned long)st->shapenum);
    mix((unsigned long)st->ticcount);
    mix((unsigned long)st->hitpoints);
    mix((unsigned long)st->count);
  }
  mix((unsigned long)n);
  part[2] = check_hash;
  check_hash = 2166136261UL;
  for (i = 0; i < doornum; i++) {
    mix((unsigned long)doorobjlist[i]->position);
    mix(doorobjlist[i]->action);
    mix(doorobjlist[i]->flags);
    mix((unsigned long)doorobjlist[i]->ticcount);
  }
  for (i = 0; i < pwallnum; i++) {
    mix((unsigned long)pwallobjlist[i]->x);
    mix((unsigned long)pwallobjlist[i]->y);
    mix(pwallobjlist[i]->action);
    mix((unsigned long)pwallobjlist[i]->state);
    mix(pwallobjlist[i]->dir);
  }
  for (i = 0; i < maskednum; i++) mix(maskobjlist[i]->flags);
  part[3] = check_hash;

  sprintf(line, "%d %08lx %08lx %08lx %08lx %d %d | %lx %lx %d %d %d %d %ld\r\n", tic, part[0], part[1],
          part[2], part[3], na, n, (long)player->x, (long)player->y, player->angle,
          player->momentumx, player->momentumy, GetRNGindex(), (long)gamestate.TimeCount);
  Fwrite((short)check_file, strlen(line), line);
}

#endif
