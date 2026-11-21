/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef ATARI_C2P_H
#define ATARI_C2P_H

void atari_c2p_init(void);
void atari_c2p_shutdown(void);
void atari_c2p_set_palette(const unsigned char *colors);
/* How the last palette is shown, for the Accelerator to do the same: the
 * 16 palette words, and for each colour (a << 4) | b and k, k of a Bayer
 * cell's 16 thresholds getting pen b and the rest pen a (MD_PAL_PENS). */
void atari_c2p_pens(unsigned short *words, unsigned char (*mix)[2]);
/* Whether the game palette may come from atari_palette.c's table (it may
 * at first): not while an Accelerator that can't be told how (firmware
 * without MD_CAP_PENS) shares the screen. The next palette follows it. */
void atari_c2p_use_table(int on);
void atari_c2p_set_fast_mode(int enable);
void atari_c2p_screen(unsigned char *out, const unsigned char *in, int zoom, int center_x, int center_y,
                      int view_x, int view_y, int view_w, int view_h,
                      int protect_top, int protect_bottom);
/* The renderer drew the whole of this rectangle of the chunky screen afresh
 * (the 3D view, each frame of play): the next atari_c2p_screen converts it
 * without first comparing it with the last frame's, which it would find
 * changed nearly everywhere, and keeps no copy of it. */
void atari_c2p_view_drawn(int x, int y, int w, int h);
/*
 * Low detail: the view drawn at its top-left `view`, (x, y) on the
 * screen, w x h, comes in pairs of the same pixel from an even column but
 * where a draw says otherwise; a frame's conversion of the view then takes
 * those parts a pair at a time (atari_c2p.c). Anything that draws into the
 * view between this and the frame's conversion and is not a pair from an
 * even column must say so:
 *  - atari_unpair_col(buf, y0, y1): a column at buf (its view row 0),
 *    view rows y0 to y1 (inclusive; clipped to the view);
 *  - atari_unpair_rect: screen columns x0-x1 and rows y0-y1 (inclusive);
 *  - atari_unpair_view: all of it.
 * Missing one shows that thing's pixels doubled up.
 */
void atari_c2p_pairs_begin(unsigned char *view, int x, int y, int w, int h);
void atari_unpair_rect(int x0, int x1, int y0, int y1);
void atari_unpair_view(void);
void atari_unpair_col_slow(const unsigned char *buf, int y0, int y1);
extern unsigned char *atari_pair_base;
#define atari_unpair_col(buf, y0, y1) \
   do { if (atari_pair_base) atari_unpair_col_slow((const unsigned char *)(buf), (y0), (y1)); } while (0)
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
