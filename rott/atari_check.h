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
 *
 * The picture is checked the same way: after each frame is drawn, a hash
 * of the chunky screen (the view's rows, and all of it) goes to
 * C:\RENDER.TXT, and every 25th frame's 320x200 pixels to RENDnnn.RAW, so
 * two builds of the ST renderer that draw the same pixels write the same
 * files, and where they do not the frames can be compared
 * (tools/bench/rawdiff.py).
 */
#ifndef ATARI_CHECK_H
#define ATARI_CHECK_H

#ifndef ATARI_LOGIC_CHECK
#define ATARI_LOGIC_CHECK 0
#endif

#if defined(ATARI_NATIVE) && (ATARI_LOGIC_CHECK > 0)
extern int atari_check_clock_on;
extern int atari_check_clock;
void ATARI_CheckStart(void);   /* PlayLoop, before the controls start */
void ATARI_CheckAdvance(void); /* CalcTics: the next frame's tics */
void ATARI_CheckKeys(void);    /* PollControls: this frame's controls */
void ATARI_CheckFrame(void);   /* after the frame's game logic */
void ATARI_CheckRender(void);  /* after the frame is drawn */
#endif

#endif
