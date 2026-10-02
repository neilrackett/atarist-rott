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

#if defined(__MINT__)
/* doEvents: outside play, the pad presses keys (menus, waits for a key). */
void ATARI_XpadPump(void);
/* PollControls: in play, the pad's buttons (after the keyboard's) and its
 * left stick's turning and movement (just before PollMove). */
void ATARI_XpadButtons(void);
void ATARI_XpadMove(void);

/* modexlib.c: a key going down or up, as if from the keyboard. */
void ATARI_InjectKey(int scancode, int down);
/* rt_build.c: a menu is up. */
int ATARI_MenuActive(void);
#endif

#endif
