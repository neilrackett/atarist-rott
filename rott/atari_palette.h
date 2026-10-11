/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * atari_palette.h - the 16 colours ROTT's game palette is shown with, and
 * each of its 256 colours' dither, chosen offline for what the game draws
 * (tools/rottpalette): one table for the STE's 4096 colours (and the TT's
 * and Falcon's ST modes), one for the ST's 512. Used by atari_c2p.c; the
 * ROTT Accelerator is sent the result with each palette (MD_PAL_PENS).
 */
#ifndef ATARI_PALETTE_H
#define ATARI_PALETTE_H

/* atari_c2p_set_palette's hash (FNV-1a) of the game palette, 8 bits a
 * channel: any other palette is quantised as before. */
extern const unsigned long atari_palette_hash;

/* Per machine: the palette registers, darkest first; each colour as 8-bit
 * RGB, as shown; and for each of the 256 colours, {(a << 4) | b, k}: of
 * each 4x4 Bayer cell's 16 thresholds, 16 - k get pen a and k pen b (a is
 * the darker; k = 0 is a alone). */
extern const unsigned short atari_palette_ste_words[16];
extern const unsigned char atari_palette_ste_rgb[16][3];
extern const unsigned char atari_palette_ste_mix[256][2];
extern const unsigned short atari_palette_st_words[16];
extern const unsigned char atari_palette_st_rgb[16][3];
extern const unsigned char atari_palette_st_mix[256][2];

#endif
