/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
/*
 * atari_check.h - ATARI_LOGIC_CHECK=n test builds: start a game, run it on
 * a virtual clock that moves n tics a frame, with scripted controls, and
 * write a hash of the game state after every frame to C:\LOGIC.TXT. Two
 * builds that leave the game logic alone write the same file, however
 * fast each one is.
 */
#ifndef ATARI_CHECK_H
#define ATARI_CHECK_H

#ifndef ATARI_LOGIC_CHECK
#define ATARI_LOGIC_CHECK 0
#endif

#if defined(__MINT__) && (ATARI_LOGIC_CHECK > 0)
extern int atari_check_clock_on;
extern int atari_check_clock;
void ATARI_CheckStart(void);   /* PlayLoop, before the controls start */
void ATARI_CheckAdvance(void); /* CalcTics: the next frame's tics */
void ATARI_CheckKeys(void);    /* PollControls: this frame's controls */
void ATARI_CheckFrame(void);   /* after the frame's game logic */
#endif

#endif
