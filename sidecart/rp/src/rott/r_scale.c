/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * File: r_scale.c
 * Description: Scaled sprites, the weapon and the column drawers, from
 *              atarist-rott's rt_scale.c. The player's own light level uses
 *              the view position (the ST does not send the player's).
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

#include "r_local.h"

const byte *shadingtable;
int dc_texturemid;
int dc_iscale;
int dc_invscale;
int sprtopoffset;
int dc_yl;
int dc_yh;
const byte *dc_source;
int centeryclipped;

/* ------------------------------------------------------------------ */
/* Column drawers (rt_scale.c, the non-DOS versions)                    */
/* ------------------------------------------------------------------ */

static void R_DrawColumn(byte *buf) {
  int count;
  int frac, fracstep;
  byte *dest;
  const byte *src = dc_source;
  const byte *shade = shadingtable;

  count = dc_yh - dc_yl + 1;
  if (count < 0) return;

  dest = buf + dc_yl * R_PITCH;

  fracstep = dc_iscale;
  frac = dc_texturemid + (dc_yl - centery) * fracstep;

  while (count--) {
    *dest = shade[src[(frac >> SFRACBITS)]];
    dest += R_PITCH;
    frac += fracstep;
  }
}

static void R_TransColumn(byte *buf) {
  int count;
  byte *dest;

  count = dc_yh - dc_yl + 1;
  if (count < 0) return;

  dest = buf + dc_yl * R_PITCH;

  while (count--) {
    *dest = shadingtable[*dest];
    dest += R_PITCH;
  }
}

void R_DrawWallColumn(byte *buf) {
  int count;
  int frac, fracstep;
  byte *dest;
  const byte *src = dc_source;
  const byte *shade = shadingtable;

  count = dc_yh - dc_yl;
  if (count < 0) return;

  dest = buf + dc_yl * R_PITCH;

  fracstep = dc_iscale;
  frac = dc_texturemid + (dc_yl - centery) * fracstep;
  frac <<= 10;
  fracstep <<= 10;

  while (count--) {
    *dest = shade[src[(((unsigned)frac) >> 26)]];
    dest += R_PITCH;
    frac += fracstep;
  }
}

static void R_DrawClippedColumn(byte *buf) {
  int count;
  int frac, fracstep;
  byte *dest;
  const byte *src = dc_source;
  const byte *shade = shadingtable;

  count = dc_yh - dc_yl + 1;
  if (count < 0) return;

  dest = buf + dc_yl * R_PITCH;

  fracstep = dc_iscale;
  frac = dc_texturemid + (dc_yl - centeryclipped) * fracstep;

  while (count--) {
    *dest = shade[src[(((unsigned)frac) >> SFRACBITS)]];
    dest += R_PITCH;
    frac += fracstep;
  }
}

static void R_DrawSolidColumn(int color, byte *buf) {
  int count;
  byte *dest;

  count = dc_yh - dc_yl + 1;
  if (count < 0) return;

  dest = buf + dc_yl * R_PITCH;

  while (count--) {
    *dest = (byte)color;
    dest += R_PITCH;
  }
}

/* ------------------------------------------------------------------ */
/* Light levels                                                         */
/* ------------------------------------------------------------------ */

void SetPlayerLightLevel(void) {
  int i;
  int lv;
  int intercept;
  int height;

  if (gason) {
    shadingtable = greenmap + (gasindex << 8);
    return;
  }

  if (fulllight || fog) {
    shadingtable = colormap + (1 << 12);
    return;
  }

  height = PLAYERHEIGHT;

  if (viewangle < FINEANGLES / 8 || viewangle > 7 * FINEANGLES / 8)
    intercept = (viewx >> 11) & 0x1c;
  else if (viewangle < 3 * FINEANGLES / 8)
    intercept = (viewy >> 11) & 0x1c;
  else if (viewangle < 5 * FINEANGLES / 8)
    intercept = (viewx >> 11) & 0x1c;
  else
    intercept = (viewy >> 11) & 0x1c;

  if (lightsource) {
    lv = (((R_LightAt(viewx >> 16, viewy >> 16) >> intercept) & 0xf) >> 1);
    i = maxshade - (height >> normalshade) - lv;
    if (i < minshade) i = minshade;
    shadingtable = colormap + (i << 8);
  } else {
    i = maxshade - (height >> normalshade);
    if (i < minshade) i = minshade;
    shadingtable = colormap + (i << 8);
  }
}

void SetLightLevel(int height) {
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

/* ------------------------------------------------------------------ */
/* Posts                                                                */
/* ------------------------------------------------------------------ */

void ScaleTransparentPost(const byte *src, byte *buf, int level) {
  int offset;
  int length;
  int topscreen;
  int bottomscreen;
  const byte *oldlevel;
  const byte *seelevel;

  seelevel = colormap + (((level + 64) >> 2) << 8);
  oldlevel = shadingtable;
  offset = *(src++);
  for (; offset != 255;) {
    length = *(src++);
    topscreen = sprtopoffset + (dc_invscale * offset);
    bottomscreen = topscreen + (dc_invscale * length);
    dc_yl = (topscreen + SFRACUNIT) >> SFRACBITS;
    dc_yh = ((bottomscreen - 1) >> SFRACBITS);
    if (dc_yh >= viewheight) dc_yh = viewheight - 1;
    if (dc_yl < 0) dc_yl = 0;
    if ((*src) == 254) {
      shadingtable = seelevel;
      if (dc_yl <= dc_yh) R_TransColumn(buf);
      src++;
      offset = *(src++);
      shadingtable = oldlevel;
    } else {
      if (dc_yl <= dc_yh) {
        dc_source = src - offset;
        R_DrawColumn(buf);
      }
      src += length;
      offset = *(src++);
    }
  }
}

void ScaleMaskedPost(const byte *src, byte *buf) {
  int offset;
  int length;
  int topscreen;
  int bottomscreen;

  offset = *(src++);
  for (; offset != 255;) {
    length = *(src++);
    topscreen = sprtopoffset + (dc_invscale * offset);
    bottomscreen = topscreen + (dc_invscale * length);
    dc_yl = (topscreen + SFRACUNIT) >> SFRACBITS;
    dc_yh = ((bottomscreen - 1) >> SFRACBITS);
    if (dc_yh >= viewheight) dc_yh = viewheight - 1;
    if (dc_yl < 0) dc_yl = 0;
    if (dc_yl <= dc_yh) {
      dc_source = src - offset;
      R_DrawColumn(buf);
    }
    src += length;
    offset = *(src++);
  }
}

static void ScaleClippedPost(const byte *src, byte *buf) {
  int offset;
  int length;
  int topscreen;
  int bottomscreen;

  offset = *(src++);
  for (; offset != 255;) {
    length = *(src++);
    topscreen = sprtopoffset + (dc_invscale * offset);
    bottomscreen = topscreen + (dc_invscale * length);
    dc_yl = (topscreen + SFRACUNIT - 1) >> SFRACBITS;
    dc_yh = ((bottomscreen - 1) >> SFRACBITS);
    if (dc_yh >= viewheight) dc_yh = viewheight - 1;
    if (dc_yl < 0) dc_yl = 0;
    if (dc_yl <= dc_yh) {
      dc_source = src - offset;
      R_DrawClippedColumn(buf);
    }
    src += length;
    offset = *(src++);
  }
}

static void ScaleSolidMaskedPost(int color, const byte *src, byte *buf) {
  int offset;
  int length;
  int topscreen;
  int bottomscreen;

  offset = *(src++);
  for (; offset != 255;) {
    length = *(src++);
    topscreen = sprtopoffset + (dc_invscale * offset);
    bottomscreen = topscreen + (dc_invscale * length);
    dc_yl = (topscreen + SFRACUNIT) >> SFRACBITS;
    dc_yh = ((bottomscreen - 1) >> SFRACBITS);
    if (dc_yh >= viewheight) dc_yh = viewheight - 1;
    if (dc_yl < 0) dc_yl = 0;
    if (dc_yl <= dc_yh) {
      dc_source = src - offset;
      R_DrawSolidColumn(color, buf);
    }
    src += length;
    offset = *(src++);
  }
}

static void ScaleMaskedWidePost(const byte *src, byte *buf, int x, int width) {
  buf += x;
  while (width--) {
    ScaleMaskedPost(src, buf);
    buf++;
  }
}

/* ------------------------------------------------------------------ */
/* Shapes                                                               */
/* ------------------------------------------------------------------ */

void ScaleShape(visobj_t *sprite) {
  const byte *shape;
  int frac;
  const patch_t *p;
  int x1, x2;
  int tx;
  int size;

  shape = R_Lump(sprite->shapenum);
  if (!shape) return;
  p = (const patch_t *)shape;
  size = p->origsize >> 7;
  dc_invscale = sprite->viewheight << ((10 - HEIGHTFRACTION) - size);
  if (dc_invscale <= 0) return;
  tx = -p->leftoffset;
  sprite->viewx = (sprite->viewx << SFRACBITS) -
                  (sprite->viewheight << (SFRACBITS - HEIGHTFRACTION - 1)) +
                  (SFRACUNIT >> 1);

  x1 = (sprite->viewx + (tx * dc_invscale)) >> SFRACBITS;
  if (x1 >= viewwidth) return;
  tx += p->width;
  x2 = ((sprite->viewx + (tx * dc_invscale)) >> SFRACBITS) - 1;
  if (x2 < 0) return;

  dc_iscale = (int)(0xffffffffu / (unsigned)dc_invscale);
  dc_texturemid = (((sprite->h1 << size) + p->topoffset) << SFRACBITS);
  sprtopoffset = centeryfrac - FixedMul(dc_texturemid, dc_invscale);
  shadingtable = sprite->colormap;

  if (x1 < 0) {
    frac = dc_iscale * (-x1);
    x1 = 0;
  } else {
    frac = 0;
  }
  x2 = x2 >= viewwidth ? viewwidth - 1 : x2;

  if (sprite->viewheight > ((1 << (HEIGHTFRACTION + 6)) << size)) {
    int texturecolumn;
    int lastcolumn;
    int startx;
    int width;

    width = 1;
    startx = 0;
    lastcolumn = -1;
    for (; x1 <= x2; x1++, frac += dc_iscale) {
      if (posts[x1].wallheight > sprite->viewheight) {
        if (lastcolumn >= 0) {
          ScaleMaskedWidePost(((p->collumnofs[lastcolumn]) + shape), r_screen,
                              startx, width);
          width = 1;
          lastcolumn = -1;
        }
        continue;
      }
      texturecolumn = frac >> SFRACBITS;
      if (texturecolumn >= p->width) texturecolumn = p->width - 1;
      if ((texturecolumn == lastcolumn) && (width < 9)) {
        width++;
        continue;
      } else {
        if (lastcolumn >= 0) {
          ScaleMaskedWidePost(((p->collumnofs[lastcolumn]) + shape), r_screen,
                              startx, width);
          width = 1;
          startx = x1;
          lastcolumn = texturecolumn;
        } else {
          startx = x1;
          lastcolumn = texturecolumn;
        }
      }
    }
    if (lastcolumn != -1)
      ScaleMaskedWidePost(((p->collumnofs[lastcolumn]) + shape), r_screen,
                          startx, width);
  } else {
    byte *b = r_screen + x1;
    for (; x1 <= x2; x1++, frac += dc_iscale, b++) {
      int col;
      if (posts[x1].wallheight > sprite->viewheight) continue;
      col = frac >> SFRACBITS;
      if (col >= p->width) col = p->width - 1;
      ScaleMaskedPost(((p->collumnofs[col]) + shape), b);
    }
  }
}

void ScaleTransparentShape(visobj_t *sprite) {
  const byte *shape;
  int frac;
  const transpatch_t *p;
  int x1, x2;
  int tx;
  int size;
  byte *b;

  shape = R_Lump(sprite->shapenum);
  if (!shape) return;
  p = (const transpatch_t *)shape;
  size = p->origsize >> 7;
  dc_invscale = sprite->viewheight << ((10 - HEIGHTFRACTION) - size);
  if (dc_invscale <= 0) return;
  tx = -p->leftoffset;
  sprite->viewx = (sprite->viewx << SFRACBITS) -
                  (sprite->viewheight << (SFRACBITS - HEIGHTFRACTION - 1));

  x1 = (sprite->viewx + (tx * dc_invscale)) >> SFRACBITS;
  if (x1 >= viewwidth) return;
  tx += p->width;
  x2 = ((sprite->viewx + (tx * dc_invscale)) >> SFRACBITS) - 1;
  if (x2 < 0) return;

  dc_iscale = (int)(0xffffffffu / (unsigned)dc_invscale);
  dc_texturemid = (((sprite->h1 << size) + p->topoffset) << SFRACBITS);
  sprtopoffset = centeryfrac - FixedMul(dc_texturemid, dc_invscale);
  shadingtable = sprite->colormap;

  if (x1 < 0) {
    frac = dc_iscale * (-x1);
    x1 = 0;
  } else {
    frac = 0;
  }
  x2 = x2 >= viewwidth ? viewwidth - 1 : x2;

  b = r_screen + x1;
  for (; x1 <= x2; x1++, frac += dc_iscale, b++) {
    int col;
    if (posts[x1].wallheight > sprite->viewheight) continue;
    col = frac >> SFRACBITS;
    if (col >= p->width) col = p->width - 1;
    ScaleTransparentPost(((p->collumnofs[col]) + shape), b, sprite->h2);
  }
}

void ScaleSolidShape(visobj_t *sprite) {
  const byte *shape;
  int frac;
  const patch_t *p;
  int x1, x2;
  int tx;
  int size;
  byte *b;

  shape = R_Lump(sprite->shapenum);
  if (!shape) return;
  p = (const patch_t *)shape;
  size = p->origsize >> 7;
  dc_invscale = sprite->viewheight << ((10 - HEIGHTFRACTION) - size);
  if (dc_invscale <= 0) return;
  tx = -p->leftoffset;
  sprite->viewx = (sprite->viewx << SFRACBITS) -
                  (sprite->viewheight << (SFRACBITS - HEIGHTFRACTION - 1)) +
                  (SFRACUNIT >> 1);

  x1 = (sprite->viewx + (tx * dc_invscale)) >> SFRACBITS;
  if (x1 >= viewwidth) return;
  tx += p->width;
  x2 = ((sprite->viewx + (tx * dc_invscale)) >> SFRACBITS) - 1;
  if (x2 < 0) return;

  dc_iscale = (int)(0xffffffffu / (unsigned)dc_invscale);
  dc_texturemid = (((sprite->h1 << size) + p->topoffset) << SFRACBITS);
  sprtopoffset = centeryfrac - FixedMul(dc_texturemid, dc_invscale);
  shadingtable = sprite->colormap;

  if (x1 < 0) {
    frac = dc_iscale * (-x1);
    x1 = 0;
  } else {
    frac = 0;
  }
  x2 = x2 >= viewwidth ? viewwidth - 1 : x2;

  b = r_screen + x1;
  for (; x1 <= x2; x1++, frac += dc_iscale, b++) {
    int col;
    if (posts[x1].wallheight > sprite->viewheight) continue;
    col = frac >> SFRACBITS;
    if (col >= p->width) col = p->width - 1;
    ScaleSolidMaskedPost(sprite->h2, ((p->collumnofs[col]) + shape), b);
  }
}

void ScaleWeapon(int xoff, int y, int shapenum) {
  const byte *shape;
  int frac;
  int h;
  const patch_t *p;
  int x1, x2;
  int tx;
  int xcent;
  byte *b;

  SetPlayerLightLevel();
  shape = R_Lump(shapenum);
  if (!shape) return;
  p = (const patch_t *)shape;
  if (p->origsize <= 0) return;
  h = ((p->origsize * weaponscale) >> 17);
  centeryclipped = (viewheight - h) + FixedMul(y, weaponscale);
  xcent = centerx + FixedMul(xoff, weaponscale);
  dc_invscale = (h << 17) / p->origsize;
  if (dc_invscale <= 0) return;

  tx = -p->leftoffset;
  xcent = (xcent << SFRACBITS) - (h << SFRACBITS);

  x1 = (xcent + (tx * dc_invscale)) >> SFRACBITS;
  if (x1 >= viewwidth) return;
  tx += p->width;
  x2 = ((xcent + (tx * dc_invscale)) >> SFRACBITS) - 1;
  if (x2 < 0) return;

  dc_iscale = (int)(0xffffffffu / (unsigned)dc_invscale);
  dc_texturemid =
      (((p->origsize >> 1) + p->topoffset) << SFRACBITS) + (SFRACUNIT >> 1);
  sprtopoffset = (centeryclipped << 16) - FixedMul(dc_texturemid, dc_invscale);

  if (x1 < 0) {
    frac = dc_iscale * (-x1);
    x1 = 0;
  } else {
    frac = 0;
  }

  x2 = x2 >= viewwidth ? viewwidth - 1 : x2;

  b = r_screen + x1;
  for (; x1 <= x2; x1++, frac += dc_iscale, b++) {
    int col = frac >> SFRACBITS;
    if (col >= p->width) col = p->width - 1;
    ScaleClippedPost(((p->collumnofs[col]) + shape), b);
  }
}

void DrawScreenSprite(int x, int y, int shapenum) {
  ScaleWeapon(x - 160, y - 200, shapenum);
}

void DrawScreenSizedSprite(int lump) {
  const byte *shape;
  int frac;
  const patch_t *p;
  int x1, x2;
  int tx;
  byte *b;

  shadingtable = colormap + (1 << 12);
  shape = R_Lump(lump);
  if (!shape) return;
  p = (const patch_t *)shape;
  if (p->origsize <= 0) return;
  dc_invscale = (viewwidth << 16) / p->origsize;
  if (dc_invscale <= 0) return;
  tx = -p->leftoffset;
  centeryclipped = viewheight >> 1;

  x1 = (tx * dc_invscale) >> SFRACBITS;
  if (x1 >= viewwidth) return;
  tx += p->width;
  x2 = ((tx * dc_invscale) >> SFRACBITS) - 1;
  if (x2 < 0) return;

  dc_iscale = (int)(0xffffffffu / (unsigned)dc_invscale);
  dc_texturemid =
      (((p->origsize >> 1) + p->topoffset) << SFRACBITS) + (SFRACUNIT >> 1);
  sprtopoffset = (centeryclipped << 16) - FixedMul(dc_texturemid, dc_invscale);

  x2 = (viewwidth - 1);
  frac = 0;
  b = r_screen;
  for (x1 = 0; x1 <= x2; x1++, frac += dc_iscale, b++) {
    int col = frac >> SFRACBITS;
    if (col >= p->width) col = p->width - 1;
    ScaleClippedPost(((p->collumnofs[col]) + shape), b);
  }
}
