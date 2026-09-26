/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * File: md_video.c
 * Description: 256 -> 16 colour reduction and chunky-to-planar conversion
 *              of the rendered view into a ROM4 frame buffer.
 *
 * The ST shows the HUD it converts itself and the view the MD converted,
 * under ONE hardware palette, so both must reduce ROTT's palette to the
 * same 16 colours and dither identically. The reduction below is
 * atari_c2p.c's (median cut, two-nearest weights, 4x4 Bayer), ported
 * line for line; atari_c2p.c's colour sort breaks ties by index so that
 * the result does not depend on either side's qsort.
 *
 * GPL-2.0-or-later because it carries atari_c2p.c's code.
 */

#include "md_video.h"

#include <stdlib.h>
#include <string.h>

#include "md_core1.h"
#include "rott_md_protocol.h"

typedef struct {
  unsigned char r;
  unsigned char g;
  unsigned char b;
} Color;

typedef struct {
  int start;
  int count;
  unsigned char rmin, rmax;
  unsigned char gmin, gmax;
  unsigned char bmin, bmax;
} ColorBox;

static Color palette16[16];
static uint16_t s_st_colors[16];

/* Scratch for a palette rebuild, lent by the caller (the view buffer,
 * which is idle between frames) to keep 6 KB out of RAM. */
typedef struct {
  unsigned char weights256[256][16];
  unsigned char scaled[256 * 3];
  Color src[256];
  unsigned char idx[256];
} palette_scratch_t;
static palette_scratch_t *S;
#define weights256 (S->weights256)

/* s_lut[cell][index] -> pen, cell = ((y & 3) << 2) | (x & 3). */
static uint8_t s_lut[16][256] __attribute__((aligned(4)));

/* ------------------------------------------------------------------ */
/* atari_c2p.c                                                          */
/* ------------------------------------------------------------------ */

static const Color *sort_colors = NULL;
static int sort_channel = 0;

static int color_cmp(const void *a, const void *b) {
  unsigned char ia = *(const unsigned char *)a;
  unsigned char ib = *(const unsigned char *)b;
  int d;
  if (sort_channel == 0)
    d = (int)sort_colors[ia].r - (int)sort_colors[ib].r;
  else if (sort_channel == 1)
    d = (int)sort_colors[ia].g - (int)sort_colors[ib].g;
  else
    d = (int)sort_colors[ia].b - (int)sort_colors[ib].b;
  return d ? d : (int)ia - (int)ib;
}

static void update_box(ColorBox *box, const Color *colors,
                       const unsigned char *idx) {
  unsigned char rmin = 255, rmax = 0;
  unsigned char gmin = 255, gmax = 0;
  unsigned char bmin = 255, bmax = 0;
  int i;
  for (i = 0; i < box->count; ++i) {
    const Color *c = &colors[idx[box->start + i]];
    if (c->r < rmin) rmin = c->r;
    if (c->r > rmax) rmax = c->r;
    if (c->g < gmin) gmin = c->g;
    if (c->g > gmax) gmax = c->g;
    if (c->b < bmin) bmin = c->b;
    if (c->b > bmax) bmax = c->b;
  }
  box->rmin = rmin;
  box->rmax = rmax;
  box->gmin = gmin;
  box->gmax = gmax;
  box->bmin = bmin;
  box->bmax = bmax;
}

static void build_palette16(const unsigned char *colors, Color *out16) {
  Color *src = S->src;
  unsigned char *idx = S->idx;
  ColorBox boxes[16];
  int box_count = 1;
  int i;

  for (i = 0; i < 256; ++i) {
    src[i].r = colors[i * 3 + 0];
    src[i].g = colors[i * 3 + 1];
    src[i].b = colors[i * 3 + 2];
    idx[i] = (unsigned char)i;
  }

  boxes[0].start = 0;
  boxes[0].count = 256;
  update_box(&boxes[0], src, idx);

  while (box_count < 16) {
    int best = -1;
    int best_range = -1;
    for (i = 0; i < box_count; ++i) {
      int rrange = (int)boxes[i].rmax - (int)boxes[i].rmin;
      int grange = (int)boxes[i].gmax - (int)boxes[i].gmin;
      int brange = (int)boxes[i].bmax - (int)boxes[i].bmin;
      int range = rrange;
      if (grange > range) range = grange;
      if (brange > range) range = brange;
      if (boxes[i].count > 1 && range > best_range) {
        best_range = range;
        best = i;
      }
    }
    if (best < 0) break;

    int rrange = (int)boxes[best].rmax - (int)boxes[best].rmin;
    int grange = (int)boxes[best].gmax - (int)boxes[best].gmin;
    int brange = (int)boxes[best].bmax - (int)boxes[best].bmin;
    if (rrange >= grange && rrange >= brange)
      sort_channel = 0;
    else if (grange >= rrange && grange >= brange)
      sort_channel = 1;
    else
      sort_channel = 2;

    sort_colors = src;
    qsort(idx + boxes[best].start, (size_t)boxes[best].count,
          sizeof(unsigned char), color_cmp);

    int half = boxes[best].count / 2;
    ColorBox newbox;
    newbox.start = boxes[best].start + half;
    newbox.count = boxes[best].count - half;
    boxes[best].count = half;

    update_box(&boxes[best], src, idx);
    update_box(&newbox, src, idx);

    boxes[box_count++] = newbox;
  }

  for (i = 0; i < box_count; ++i) {
    unsigned int rsum = 0, gsum = 0, bsum = 0;
    int j;
    for (j = 0; j < boxes[i].count; ++j) {
      const Color *c = &src[idx[boxes[i].start + j]];
      rsum += c->r;
      gsum += c->g;
      bsum += c->b;
    }
    if (boxes[i].count > 0) {
      out16[i].r = (unsigned char)(rsum / (unsigned int)boxes[i].count);
      out16[i].g = (unsigned char)(gsum / (unsigned int)boxes[i].count);
      out16[i].b = (unsigned char)(bsum / (unsigned int)boxes[i].count);
    } else {
      out16[i].r = out16[i].g = out16[i].b = 0;
    }
  }

  for (; i < 16; ++i) {
    out16[i] = out16[0];
  }
}

static void build_weights(const unsigned char *colors, int dither) {
  int i, j;
  for (i = 0; i < 256; ++i) {
    int best = -1, second = -1;
    unsigned int bestd = 0xffffffffu, secondd = 0xffffffffu;
    unsigned int r = colors[i * 3 + 0];
    unsigned int g = colors[i * 3 + 1];
    unsigned int b = colors[i * 3 + 2];
    for (j = 0; j < 16; ++j) {
      int dr = (int)r - (int)palette16[j].r;
      int dg = (int)g - (int)palette16[j].g;
      int db = (int)b - (int)palette16[j].b;
      unsigned int d = (unsigned int)(dr * dr + dg * dg + db * db);
      if (d < bestd) {
        second = best;
        secondd = bestd;
        best = j;
        bestd = d;
      } else if (d < secondd) {
        second = j;
        secondd = d;
      }
    }

    for (j = 0; j < 16; ++j) weights256[i][j] = 0;

    if (best >= 0 && bestd == 0) {
      weights256[i][best] = 16;
    } else if (best >= 0 && second >= 0 && dither) {
      unsigned int denom = bestd + secondd;
      unsigned int w0 = denom ? (16u * secondd) / denom : 16u;
      unsigned int w1 = 16u - w0;
      weights256[i][best] = (unsigned char)w0;
      weights256[i][second] = (unsigned char)w1;
    } else if (best >= 0) {
      weights256[i][best] = 16;
    }
  }
}

static unsigned short convert_channel(unsigned char v) {
  unsigned short r = (v & 0xe0) >> 5;
  r |= (v & 0x10) >> 1;
  return r;
}

static unsigned short stcolor(unsigned char r, unsigned char g,
                              unsigned char b) {
  unsigned short entry = convert_channel(r);
  entry <<= 4;
  entry |= convert_channel(g);
  entry <<= 4;
  entry |= convert_channel(b);
  return entry;
}

static void build_noir_palette16(Color *out16, unsigned short *stpalette) {
  int i;
  for (i = 0; i < 16; ++i) {
    unsigned char v = (unsigned char)((i * 255 + 7) / 15);
    out16[i].r = v;
    out16[i].g = v;
    out16[i].b = v;
    stpalette[i] = stcolor(v, v, v);
  }
}

static short bayer4_color(const unsigned char *weights, short numcolors,
                          short phase, short px) {
  static const unsigned char bayer[4][4] = {
      {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
  unsigned char bayer_lwb = 0, bayer_upb = 0;
  short c;
  for (c = 0; c < numcolors; ++c) {
    bayer_upb += weights[c];
    if (bayer[phase][px & 3] >= bayer_lwb && bayer[phase][px & 3] < bayer_upb) {
      return c;
    }
    bayer_lwb += weights[c];
  }
  return -1;
}

/* ------------------------------------------------------------------ */

void md_video_set_palette(const uint8_t *rgb768, unsigned flags,
                          void *scratch) {
  S = (palette_scratch_t *)scratch;
  unsigned char *scaled = S->scaled;
  const unsigned char *colors = rgb768;
  unsigned char maxv = 0;
  int i;
  const int noir = (flags & MD_PAL_NOIR) != 0;
  const int noir_dither = (flags & MD_PAL_NOIR_DITHER) != 0;

  for (i = 0; i < 256 * 3; ++i) {
    if (colors[i] > maxv) maxv = colors[i];
  }
  if (maxv <= 63) {
    for (i = 0; i < 256 * 3; ++i) scaled[i] = (unsigned char)(colors[i] << 2);
    colors = scaled;
  }

  if (noir) {
    build_noir_palette16(palette16, s_st_colors);
  } else {
    build_palette16(colors, palette16);
    for (i = 0; i < 16; ++i)
      s_st_colors[i] = stcolor(palette16[i].r, palette16[i].g, palette16[i].b);
  }

  build_weights(colors, (!noir || noir_dither));

  /* The pen c2p_1x_lorez would put at (x, y): bayer4_color with phase
   * y & 3 and px & 3 = x & 3 (the ST's 16-pixel groups start on
   * multiples of 16, and so do the MD's). -1 cannot happen (the weights
   * always sum to 16) but maps to 15 as the ST's bit tests would. */
  for (int cell = 0; cell < 16; cell++) {
    for (i = 0; i < 256; i++) {
      short c = bayer4_color(weights256[i], 16, (short)(cell >> 2),
                             (short)(cell & 3));
      s_lut[cell][i] = (uint8_t)(c < 0 ? 15 : c);
    }
  }
}

const uint16_t *md_video_st_colors(void) { return s_st_colors; }

/* ------------------------------------------------------------------ */
/* Chunky -> planar                                                     */
/* ------------------------------------------------------------------ */

/* One 16-pixel block of row `phase` (screen y & 3) into 4 plane words.
 * Pixel i lands in bit 15 - i of each plane word (md-doom's transpose). */
static inline void __not_in_flash_func(c2p_block)(uint32_t *dst,
                                                  const uint8_t *src,
                                                  unsigned phase) {
  const uint8_t *l0 = s_lut[(phase << 2) | 0u];
  const uint8_t *l1 = s_lut[(phase << 2) | 1u];
  const uint8_t *l2 = s_lut[(phase << 2) | 2u];
  const uint8_t *l3 = s_lut[(phase << 2) | 3u];
  uint32_t p01 = 0, p23 = 0; /* (plane1 << 16) | plane0, (plane3 << 16) | plane2 */
  for (unsigned g = 0; g < 4u; g++) {
    const uint32_t q = (uint32_t)l0[src[0]] | ((uint32_t)l1[src[1]] << 8) |
                       ((uint32_t)l2[src[2]] << 16) |
                       ((uint32_t)l3[src[3]] << 24);
    src += 4;
    const unsigned sh = 12u - 4u * g;
    const uint32_t n0 = (((q >> 0) & 0x01010101u) * 0x80402010u) >> 28;
    const uint32_t n1 = (((q >> 1) & 0x01010101u) * 0x80402010u) >> 28;
    const uint32_t n2 = (((q >> 2) & 0x01010101u) * 0x80402010u) >> 28;
    const uint32_t n3 = (((q >> 3) & 0x01010101u) * 0x80402010u) >> 28;
    p01 |= (n0 << sh) | (n1 << (sh + 16u));
    p23 |= (n2 << sh) | (n3 << (sh + 16u));
  }
  dst[0] = p01;
  dst[1] = p23;
}

typedef struct {
  const uint8_t *chunky;
  unsigned pitch;
  uint8_t *planar;
  unsigned width;
  unsigned screen_y;
  unsigned row0, row1;
} c2p_job_t;

static void __not_in_flash_func(c2p_rows)(const c2p_job_t *j) {
  const unsigned blocks = j->width >> 4;
  const unsigned bpr = j->width >> 1;
  for (unsigned r = j->row0; r < j->row1; r++) {
    const uint8_t *src = j->chunky + r * j->pitch;
    uint32_t *dst = (uint32_t *)(j->planar + r * bpr);
    const unsigned phase = (j->screen_y + r) & 3u;
    for (unsigned b = 0; b < blocks; b++, src += 16, dst += 2) {
      c2p_block(dst, src, phase);
    }
  }
}

static void __not_in_flash_func(c2p_core1)(void *arg) {
  c2p_rows((const c2p_job_t *)arg);
}

void __not_in_flash_func(md_video_c2p)(const uint8_t *chunky, unsigned pitch,
                                       uint8_t *planar, unsigned width,
                                       unsigned height, unsigned screen_y) {
  const unsigned mid = height >> 1;
  c2p_job_t bottom = {chunky, pitch, planar, width, screen_y, mid, height};
  c2p_job_t top = {chunky, pitch, planar, width, screen_y, 0, mid};
  md_core1_dispatch(c2p_core1, &bottom);
  c2p_rows(&top);
  md_core1_wait();
}
