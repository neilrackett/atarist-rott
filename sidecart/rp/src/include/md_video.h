/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * File: md_video.h
 * Description: 256 -> 16 colour reduction (identical to the ST's
 *              atari_c2p.c) and the chunky-to-planar conversion of the
 *              rendered view into a ROM4 frame buffer.
 */

#ifndef MD_VIDEO_H
#define MD_VIDEO_H

#include <stdint.h>

#include "pico.h"

/* Rebuild the 16 colours and the dither LUT from a 768-byte palette,
 * exactly as the ST's atari_c2p_set_palette() does. `flags` = MD_PAL_*.
 * `scratch` is MD_VIDEO_SCRATCH_BYTES of RAM the rebuild may use. */
#define MD_VIDEO_SCRATCH_BYTES 6144
void md_video_set_palette(const uint8_t *rgb768, unsigned flags,
                          void *scratch);

/* The 16 ST palette words of the last md_video_set_palette(). */
const uint16_t *md_video_st_colors(void);

/* Convert `height` rows of `width` (a multiple of 16) chunky pixels into
 * ST low-res planar rows of width / 2 bytes, top half on Core 0 and bottom
 * half on Core 1. `screen_y` is where row 0 lands on the ST's screen, so
 * the dither lines up with the HUD the ST converts. */
void __not_in_flash_func(md_video_c2p)(const uint8_t *chunky, unsigned pitch,
                                       uint8_t *planar, unsigned width,
                                       unsigned height, unsigned screen_y);

#endif /* MD_VIDEO_H */
