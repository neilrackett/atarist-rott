/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
/*
 * atari_xpad.c - gamepads through Xpad (https://github.com/neilrackett/
 * atarist-xpad, in lib/xpad): pad 0 of whatever provider a driver in the
 * AUTO folder installed. Without one, nothing here does anything.
 *
 * In play the pad works ROTT's buttons directly, so it needs no key
 * bindings, with its left stick turning and moving in proportion as ROTT's
 * own joystick code does (the d-pad as the arrow keys would). Face buttons
 * by position, named as on an Xbox pad (Xpad's XPAD_X and XPAD_Y follow
 * Linux, where BTN_X is north), as MD/DOOM has them:
 *
 *   d-pad, left stick   turn and move         A (south)  fire
 *   right stick         look up/down, strafe  B (east)   use (open, press)
 *   LT, RT              strafe left, right    X (west)   strafe
 *   LB, RB              previous, next weapon Y (north)  run
 *   left stick click    turn around           Start      menu
 *   Select              map                   Guide      pause
 *
 * Elsewhere (menus, the screens that wait for a key) it presses keys: the
 * d-pad the arrows, A Return, B and Start Escape.
 */

#include "atari_xpad.h"

#if defined(__MINT__)

#include "rt_def.h"

#include <string.h>

#include "isr.h"
#include "keyb.h"
#include "rt_actor.h"
#include "rt_playr.h"
#include "xpad.h"

/* Stick travel (of 127) ignored as noise, and the travel that counts as
 * pushed for the right stick's digital actions. */
#define XPAD_DEADZONE 24
#define XPAD_PUSHED 64

extern int JX, JY;

static const XPAD *pads;
static int looked;
static XPAD_PAD pad;          /* this frame's, or all zero */
static uint32_t keys_down;    /* buttons now held down as keys */
static uint32_t play_buttons; /* buttons seen at the last poll in play */

static void sample(void) {
  XPAD_PAD p;
  if (!looked) {
    looked = 1;
    pads = xpad_find();
  }
  if (pads && xpad_read(pads, 0, &p) && p.type != XPAD_TYPE_NONE)
    pad = p;
  else
    memset(&pad, 0, sizeof(pad));
}

static int in_play(void) {
  return controlupdatestarted && playstate == ex_stillplaying && !ATARI_MenuActive();
}

/* ------------------------------------------------------------------ */
/* Keys, outside play                                                   */
/* ------------------------------------------------------------------ */

static const struct {
  uint32_t button;
  unsigned char scancode;
} key_map[] = {
    {XPAD_UP, sc_UpArrow},    {XPAD_DOWN, sc_DownArrow}, {XPAD_LEFT, sc_LeftArrow},
    {XPAD_RIGHT, sc_RightArrow}, {XPAD_SOUTH, sc_Return}, {XPAD_EAST, sc_Escape},
    {XPAD_START, sc_Escape},
};

void ATARI_XpadPump(void) {
  uint32_t want = 0;
  unsigned i;

  sample();
  if (!pads) return;
  if (!in_play()) {
    for (i = 0; i < sizeof(key_map) / sizeof(key_map[0]); i++)
      want |= pad.buttons & key_map[i].button;
  } else {
    want = pad.buttons & XPAD_START; /* the menu, from play */
  }
  /* What went down as a key comes up as one, whatever happened between. */
  for (i = 0; i < sizeof(key_map) / sizeof(key_map[0]); i++) {
    const uint32_t b = key_map[i].button;
    if ((want ^ keys_down) & b) ATARI_InjectKey(key_map[i].scancode, (want & b) != 0);
  }
  keys_down = want;
}

/* ------------------------------------------------------------------ */
/* Play                                                                 */
/* ------------------------------------------------------------------ */

/* The next weapon held, from the current one, in the order of the 1-4
 * keys: pistol, two pistols, MP40, the missile weapon. */
static void change_weapon(int step) {
  static const int select[4] = {bt_pistol, bt_dualpistol, bt_mp40, bt_missileweapon};
  const playertype *p = locplayerstate;
  int have[4], slot, k;

  have[0] = 1;
  have[1] = p->HASBULLETWEAPON[wp_twopistol];
  have[2] = p->HASBULLETWEAPON[wp_mp40];
  have[3] = p->missileweapon != -1;
  if (p->weapon == wp_pistol)
    slot = 0;
  else if (p->weapon == wp_twopistol)
    slot = 1;
  else if (p->weapon == wp_mp40)
    slot = 2;
  else
    slot = 3;
  for (k = 1; k < 4; k++) {
    const int s = (slot + step * k + 8) & 3;
    if (have[s]) {
      buttonpoll[select[s]] = true;
      return;
    }
  }
}

/* An analogue axis, when the pad has one and it is pushed. */
static int axis(int8_t v) {
  if (!(pad.flags & XPAD_PAD_ANALOG)) return 0;
  return (v > XPAD_DEADZONE || v < -XPAD_DEADZONE) ? v : 0;
}

void ATARI_XpadButtons(void) {
  uint32_t b, pressed;

  sample();
  if (!pads) return;
  b = pad.buttons;
  pressed = b & ~play_buttons;
  play_buttons = b;
  if (!b && !(pad.flags & XPAD_PAD_ANALOG)) return;

  if (b & XPAD_SOUTH) buttonpoll[bt_attack] = true;
  if (b & XPAD_EAST) buttonpoll[bt_use] = true;
  if (b & XPAD_WEST) buttonpoll[bt_strafe] = true;
  if (b & XPAD_NORTH) buttonpoll[bt_run] = true;
  if (b & XPAD_LT) buttonpoll[bt_strafeleft] = true;
  if (b & XPAD_RT) buttonpoll[bt_straferight] = true;
  if (b & XPAD_SELECT) buttonpoll[bt_map] = true;
  if (b & XPAD_L3) buttonpoll[bt_turnaround] = true;
  if (pressed & XPAD_LB) change_weapon(-1);
  if (pressed & XPAD_RB) change_weapon(1);
  if (pressed & XPAD_GUIDE) PausePressed = true;

  /* The d-pad as the arrow keys. A provider sets these bits for the left
   * stick too, so on an axis the stick is pushed along they are its, and
   * ATARI_XpadMove works it in proportion instead. */
  if (!axis(pad.lx)) {
    if (b & XPAD_LEFT) buttonpoll[di_west] = true;
    if (b & XPAD_RIGHT) buttonpoll[di_east] = true;
  }
  if (!axis(pad.ly)) {
    if (b & XPAD_UP) buttonpoll[di_north] = true;
    if (b & XPAD_DOWN) buttonpoll[di_south] = true;
  }

  if (pad.flags & XPAD_PAD_ANALOG) {
    if (pad.ry < -XPAD_PUSHED) buttonpoll[bt_lookup] = true;
    if (pad.ry > XPAD_PUSHED) buttonpoll[bt_lookdown] = true;
    if (pad.rx < -XPAD_PUSHED) buttonpoll[bt_strafeleft] = true;
    if (pad.rx > XPAD_PUSHED) buttonpoll[bt_straferight] = true;
  }
}

/* PollJoystickMove's scaling, from the left stick. */
void ATARI_XpadMove(void) {
  int x, y;

  if (!joystickenabled) JX = JY = 0; /* else PollJoystickMove set them */
  if (!pads) return;
  x = axis(pad.lx);
  y = axis(pad.ly);
  if (x) {
    int jx = ((-x) << 13) + ((-x) << 11);
    if (buttonpoll[bt_run]) jx <<= 1;
    JX += jx;
  }
  if (y) {
    int jy = y << 4;
    if (buttonpoll[bt_run]) jy <<= 1;
    JY += jy;
  }
}

#endif
