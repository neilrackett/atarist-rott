/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
/*
 * atari_xpad.h - gamepads through Xpad (lib/xpad): pad 0 of whatever
 * provider a driver in the AUTO folder installed. See atari_xpad.c.
 */
#ifndef ATARI_XPAD_H
#define ATARI_XPAD_H

#if defined(ATARI_NATIVE)
/* doEvents: outside play, the pad presses keys (menus, waits for a key). */
void ATARI_XpadPump(void);
/* PollControls: in play, the pad's buttons (after the keyboard's) and its
 * left stick's turning and movement (just before PollMove). */
void ATARI_XpadButtons(void);
void ATARI_XpadMove(void);
/* Rumble pad 0's low and high frequency motors (0-255) for `vbls`
 * vertical blanks, if its provider can and the game is in play. */
void ATARI_XpadRumble(int low, int high, int vbls);

/* modexlib.c: a key going down or up, as if from the keyboard. */
void ATARI_InjectKey(int scancode, int down);
/* rt_build.c: a menu is up. */
int ATARI_MenuActive(void);
#else
#define ATARI_XpadRumble(low, high, vbls) ((void)0)
#endif

#endif
