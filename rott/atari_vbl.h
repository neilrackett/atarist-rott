/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
/*
 * atari_vbl.h - routines on the vertical blank for the native build, behind
 * one TOS VBL queue slot, with their hardware put back however the program
 * ends. See atari_vbl.c.
 */
#ifndef ATARI_VBL_H
#define ATARI_VBL_H

/* Call fn every VBL, in supervisor mode, until ATARI_VBL_Remove. release
 * (supervisor, register writes only, or NULL) puts back the hardware fn
 * drives: ATARI_VBL_Remove calls it, and so does the terminate vector if
 * the program ends first. Returns -1 if there is no room. */
int ATARI_VBL_Add(void (*fn)(void), void (*release)(void));

/* Stop calling fn, and call its release. */
void ATARI_VBL_Remove(void (*fn)(void));

#endif
