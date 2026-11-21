/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
/*
 * atari_eight.h - ROTT's own 256 colours on a TT or a Falcon: TT Low or a
 * Falcon's 8-bit mode, eight interleaved bitplanes, in place of ST Low's
 * 16 colours and their dither (atari_c2p.c). Native build only.
 */
#ifndef ATARI_EIGHT_H
#define ATARI_EIGHT_H

#ifndef ATARI_EIGHT
#define ATARI_EIGHT 0
#endif

#if ATARI_EIGHT

/* 1 while the 256-colour screen is up (atari_eight_enter). */
extern int atari_eight;

/* Colours in config.rot: 16 keeps ST Low on every machine. */
extern int atari_eight_colours;

/*
 * Before the Accelerator's HELLO and long before the mode set: whether this
 * machine gets the 256-colour screen (a TT or a Falcon with a colour
 * monitor, the page's ST RAM there, and not Colours 16), with the page
 * allocated, so the answer holds. atari_eight_cancel gives it up again (an
 * Accelerator that cannot send 8 planes).
 */
int atari_eight_prepare(void);
int atari_eight_wanted(void);
void atari_eight_cancel(void);

/* The mode set, if prepared: 1 with the screen up. And TOS's mode, screen
 * and colours back. */
int atari_eight_enter(void);
void atari_eight_leave(void);

/* 256 colours, 0..255 a channel, shown from the next vertical blank. */
void atari_eight_set_palette(const unsigned char *colors);
/* As atari_c2p_fade, on the 256 registers. */
void atari_eight_fade(int r, int g, int b, int amount, int vbls);
int atari_eight_fade_amount(void);

/* w x h pixels (x and w multiples of 16) at (x, y) of the 320-wide chunky
 * screen `in`, onto the screen. */
void atari_eight_convert(const unsigned char *in, int x, int y, int w, int h);
/* A whole row of 320 pixels onto picture row y; and a converted row
 * copied to another (ViewZoom's repeated rows). */
void atari_eight_convert_row(const unsigned char *row, int y);
/* Groups x to x + w of it (x and w multiples of 16). */
void atari_eight_convert_span(const unsigned char *row, int x, int w, int y);
/* Groups x to x + w of such a row whose pixels 2i and 2i + 1 are the same
 * (x and w multiples of 16): STDL's pair conversion (STDL_SDL2_ConvertBoxPairs). */
void atari_eight_convert_pairs(const unsigned char *row, int x, int w, int y);
/* Rows y to y + h of such pairs, groups x to x + w, in one call. */
void atari_eight_convert_pairs_box(const unsigned char *in, int x, int w, int y, int h);
void atari_eight_copy_row(int from, int to);
/* The other way, any rectangle: what the screen shows, as its colours. */
void atari_eight_to_chunky(unsigned char *chunky, int x, int y, int w, int h);

/* Where picture row y starts on the screen, a byte a pixel across (16
 * bytes a group of 16), for a copy of 8-plane rows made elsewhere; a TT
 * shows each row on two lines, the second atari_eight_second bytes on (0
 * on a Falcon). */
unsigned char *atari_eight_row(int y);
extern long atari_eight_second;

/* The colours as last set, 768 bytes (test builds' screenshots). */
const unsigned char *atari_eight_colors(void);

#else

#define atari_eight 0
#define atari_eight_wanted() 0

#endif

#endif
