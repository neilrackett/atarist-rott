/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * File: r_planes.c
 * Description: Floors, ceilings and sky, from atarist-rott's rt_floor.c
 *              (DrawPlanes, DrawHLine, DrawRow, DrawSky, SetFCLightLevel)
 *              and rt_draw.c (DrawSkyPost). The Atari build draws none of
 *              this. ROTT's MakeSkyData copies the two sky lumps into a
 *              200 KB interleaved buffer; here each sky column is read
 *              straight from the two lumps in flash instead.
 */
/*
Copyright (C) 1994-1995 Apogee Software, Ltd.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.
*/

#include <string.h>

#include "r_local.h"

/* Solid colours for a plane whose lump the pack does not hold, the Atari
 * build's ATARI_FLAT_CEILING_COLOR / ATARI_FLAT_FLOOR_COLOR. */
#define FLAT_CEILING_COLOR 24
#define FLAT_FLOOR_COLOR 32

static int mr_count;
static int mr_xstep;
static int mr_ystep;
static int mr_xfrac;
static int mr_yfrac;
static byte *mr_dest;

static const byte *s_floor;
static const byte *s_ceiling;
static int xstarts[MD_VIEW_MAX_H];

static void SetFCLightLevel(int height) {
  int i;

  if (gason) {
    shadingtable = greenmap + (gasindex << 8);
    return;
  }
  if (fulllight) {
    shadingtable = colormap + (1 << 12);
    return;
  }
  if (fog) {
    i = (height >> normalshade) + minshade;
    if (i > maxshade) i = maxshade;
    shadingtable = colormap + (i << 8);
  } else {
    i = maxshade - (height >> normalshade);
    if (i < minshade) i = minshade;
    shadingtable = colormap + (i << 8);
  }
}

static void DrawRow(int count, byte *dest, const byte *src) {
  unsigned frac, fracstep;
  int coord;
  const byte *shade = shadingtable;

  frac = (mr_yfrac << 16) + (mr_xfrac & 0xffff);
  fracstep = (mr_ystep << 16) + (mr_xstep & 0xffff);

  while (count--) {
    coord = ((frac >> (32 - 7)) | ((frac >> (32 - 23)) << 7)) & 16383;
    *dest++ = shade[src[coord]];
    frac += fracstep;
  }
}

static void DrawHLine(int xleft, int xright, int yp) {
  const byte *buf;
  byte *dest;
  int startxfrac;
  int startyfrac;
  int height;

  if (yp == centery) return;
  if (xright < xleft) return;
  dest = r_screen + yp * R_PITCH;
  if (yp > centery) {
    int hd;

    buf = s_floor;
    if (!buf) {
      memset(dest + xleft, FLAT_FLOOR_COLOR, (size_t)(xright - xleft + 1));
      return;
    }
    hd = yp - centery;
    height = (hd << 13) / (maxheight - pheight + 32);
  } else {
    int hd;

    /* ROTT draws no ceiling over a sky; the sky fills it. */
    if (sky) return;
    buf = s_ceiling;
    if (!buf) {
      memset(dest + xleft, FLAT_CEILING_COLOR, (size_t)(xright - xleft + 1));
      return;
    }
    hd = centery - yp;
    height = (hd << 13) / pheight;
  }
  if (height <= 0) height = 1;
  SetFCLightLevel(height >> (8 - HEIGHTFRACTION - 1));
  mr_xstep = ((viewsin << 8) / (height));
  mr_ystep = ((viewcos << 8) / (height));

  startxfrac = ((viewx >> 1) + FixedMulShift(mr_ystep, scale, 2)) -
               FixedMulShift(mr_xstep, (centerx - xleft), 2);

  startyfrac = ((viewy >> 1) - FixedMulShift(mr_xstep, scale, 2)) -
               FixedMulShift(mr_ystep, (centerx - xleft), 2);

  mr_dest = dest + xleft;
  mr_xfrac = startxfrac;
  mr_yfrac = startyfrac;

  /* back off the pixel increment (orig. is 4x) */
  mr_xstep >>= 2;
  mr_ystep >>= 2;

  mr_count = xright - xleft + 1;
  if (mr_count) DrawRow(mr_count, mr_dest, buf);
}

/* One sky column: rows r of the 400-row strip MakeSkyData would have
 * built, bottom lump rows 0..199 then top lump rows 200..399. */
static void DrawSkyPost(byte *buf, const byte *bottom, const byte *top, int r,
                        int height) {
  const byte *shade = shadingtable;
  while (height--) {
    int rr = r;
    if (rr < 0) rr = 0;
    if (rr > 399) rr = 399;
    *buf = shade[rr < 200 ? bottom[rr] : top[rr - 200]];
    buf += R_PITCH;
    r++;
  }
}

static void DrawSky(void) {
  const byte *top = R_Lump(skytoplump);
  const byte *bottom = R_Lump(skybottomlump);
  int dest;
  int height;
  int ofs;

  if (!top || !bottom) {
    for (dest = 0; dest < viewwidth; dest++) {
      byte *b = r_screen + dest;
      for (height = posts[dest].ceilingclip; height > 0; height--) {
        *b = FLAT_CEILING_COLOR;
        b += R_PITCH;
      }
    }
    return;
  }

  if ((fog == 0) && lightning)
    shadingtable = colormap + ((basemaxshade - 6 - lightninglevel) << 8);
  else
    shadingtable = colormap + (1 << 12);

  ofs = (((maxheight) - (playerz)) >> 3) + (centery - (viewheight >> 1));
  if (ofs > centerskypost) {
    ofs = centerskypost;
  } else if (((centerskypost - ofs) + viewheight) > 1799) {
    ofs = -(1799 - (centerskypost + viewheight));
  }

  for (dest = 0; dest < viewwidth; dest++) {
    int ang;
    int col;

    if ((height = posts[dest].ceilingclip) <= 0) continue;
    ang = (viewangle + pixelangle[dest]) & (FINEANGLES - 1);
    /* skysegs[ang] = column (511 - ang % 512) >> 1 */
    col = (511 - (ang & 511)) >> 1;
    DrawSkyPost(r_screen + dest, bottom + col * 200, top + col * 200,
                centerskypost - ofs, height);
  }
}

void DrawPlanes(void) {
  int x, y;
  int twall;
  int bwall;

  s_floor = R_Lump(floorlump);
  if (s_floor) s_floor += 8;
  s_ceiling = NULL;
  if (!sky) {
    s_ceiling = R_Lump(ceilinglump);
    if (s_ceiling) s_ceiling += 8;
  }

  if (sky) {
    DrawSky();
  } else {
    y = 0;
    for (x = 0; x < viewwidth; x++) {
      twall = posts[x].ceilingclip;
      if (twall > viewheight) twall = viewheight;
      while (y < twall) {
        xstarts[y] = x;
        y++;
      }
      while (y > twall) {
        y--;
        DrawHLine(xstarts[y], x - 1, y);
      }
    }
    while (y > 0) {
      y--;
      DrawHLine(xstarts[y], viewwidth - 1, y);
    }
  }
  y = viewheight - 1;
  for (x = 0; x < viewwidth; x++) {
    bwall = posts[x].floorclip;
    if (bwall < -1) bwall = -1;
    while (y > bwall) {
      xstarts[y] = x;
      y--;
    }
    while (y < bwall) {
      y++;
      DrawHLine(xstarts[y], x - 1, y);
    }
  }
  while (y < viewheight - 1) {
    y++;
    DrawHLine(xstarts[y], viewwidth - 1, y);
  }
}
