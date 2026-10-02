/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef ATARI_C2P_H
#define ATARI_C2P_H

void atari_c2p_init(void);
void atari_c2p_shutdown(void);
void atari_c2p_set_palette(const unsigned char *colors);
void atari_c2p_set_fast_mode(int enable);
void atari_c2p_screen(unsigned char *out, const unsigned char *in, int zoom, int center_x, int center_y,
                      int view_x, int view_y, int view_w, int view_h,
                      int protect_top, int protect_bottom);
/* Fade the 16 colour registers towards r, g, b (0..255) by amount/16 over
 * vbls VBLs (0: at once). Palettes set meanwhile keep the mix, so amount 16
 * stays dark until faded back to 0. */
void atari_c2p_fade(int r, int g, int b, int amount, int vbls);
int atari_c2p_fade_amount(void);
#define ATARI_FADE_VBLS 16 /* about a third of a second */
/* The w x h pixels at (x, y) of an ST low-res screen into the chunky
 * screen, as the nearest palette colours (the MD's view, for effects that
 * work on the chunky screen). */
void atari_c2p_screen_to_chunky(const unsigned char *screen, unsigned char *chunky,
                                int x, int y, int w, int h);
/* Convert only what changed outside a view rect another renderer fills. */
void atari_c2p_hud(unsigned char *out, const unsigned char *in,
                   int view_x, int view_y, int view_w, int view_h);

#endif
