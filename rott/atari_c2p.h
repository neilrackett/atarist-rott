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

#endif
