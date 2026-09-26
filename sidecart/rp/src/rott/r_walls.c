/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * File: r_walls.c
 * Description: Walls, doors and pushwalls, from atarist-rott's rt_draw.c:
 *              TransformObject/Point/SimplePoint/Plane, CalcHeight,
 *              SetWallLightLevel, DrawWallPost, DrawWalls, TransformDoors,
 *              TransformPushWalls, InterpolateWall/Door/MaskedWall and
 *              MakeWideDoorVisible. The Atari cuts are undone: walls are lit
 *              (no ATARI_FLAT_WALL_LIGHT) and there is no minimum sprite
 *              height.
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

visobj_t vislist[MAXVISIBLE], *visptr;
visobj_t *sortedvislist[MAXVISIBLE];

static const fixed mindist = 0x1000;

/* Stands in for a wall lump the pack does not hold (it ran out of room),
 * so the column is drawn rather than left stale. */
static const byte s_missing_wall[4096] = {[0 ... 4095] = 7};

static const byte *wall_lump(int lump) {
  const byte *p = R_Lump(lump);
  return p ? p : s_missing_wall;
}

/*
========================
=
= TransformObject
=
========================
*/

bool TransformObject(int x, int y, int *dispx, int *dispheight) {
  fixed gx, gy, gxt, gyt, nx, ny;

  gx = x - viewx;
  gy = y - viewy;

  gxt = FixedMul(gx, viewcos);
  gyt = FixedMul(gy, viewsin);
  nx = gxt - gyt;

  if (nx < MINZ) return false;

  gxt = FixedMul(gx, viewsin);
  gyt = FixedMul(gy, viewcos);
  ny = gyt + gxt;

  *dispx = centerx + ny * scale / nx;
  *dispheight = (int)(heightnumerator / (uint32_t)nx);

  return true;
}

/*
========================
=
= TransformPoint
=
========================
*/

static void TransformPoint(int x, int y, int *screenx, int *height,
                           int *texture, int vertical) {
  fixed gxt, gyt, nx, ny;
  fixed gxtt, gytt;
  int gx, gy;
  int vx, vy;
  int svs, svc;

  gx = x - viewx;
  gy = y - viewy;

  gxt = FixedMul(gx, viewcos);
  gyt = FixedMul(gy, viewsin);
  nx = gxt - gyt;

  if (nx < 10) nx = 10;

  gxtt = FixedMul(gx, viewsin);
  gytt = FixedMul(gy, viewcos);
  ny = gytt + gxtt;

  *screenx = centerx + ((ny * scale) / nx);
  *height = (int)(heightnumerator / (uint32_t)nx);

  if (*screenx < 0) {
    svc = (-centerx) * viewcos;
    svs = (-centerx) * viewsin;
    vx = (scale * viewcos) + svs;
    vy = (-scale * viewsin) + svc;
    if (vertical) {
      if ((viewcos - viewsin) == 0) {
        *height = 20000 << HEIGHTFRACTION;
        return;
      }
      gy = FixedScale(gx, vy, vx);
      y = gy + viewy;
      gyt = FixedMul(gy, viewsin);
      nx = gxt - gyt;
      if (nx < 10) nx = 10;
      *screenx = 0;
      *height = (int)(heightnumerator / (uint32_t)nx);
    } else {
      if ((-viewsin - viewcos) == 0) {
        *height = 20000 << HEIGHTFRACTION;
        return;
      }
      gx = FixedScale(gy, vx, vy);
      x = gx + viewx;
      gxt = FixedMul(gx, viewcos);
      nx = gxt - gyt;
      if (nx < 10) nx = 10;
      *screenx = 0;
      *height = (int)(heightnumerator / (uint32_t)nx);
    }
  } else if (*screenx >= viewwidth) {
    svc = (centerx)*viewcos;
    svs = (centerx)*viewsin;
    vx = (scale * viewcos) + svs;
    vy = (-scale * viewsin) + svc;
    if (vertical) {
      if ((viewcos + viewsin) == 0) {
        *height = 20000 << HEIGHTFRACTION;
        return;
      }
      gy = FixedScale(gx, vy, vx);
      y = gy + viewy;
      gyt = FixedMul(gy, viewsin);
      nx = gxt - gyt;
      if (nx < 10) nx = 10;
      *screenx = viewwidth - 1;
      *height = (int)(heightnumerator / (uint32_t)nx);
    } else {
      if ((-viewsin + viewcos) == 0) {
        *height = 20000 << HEIGHTFRACTION;
        return;
      }
      gx = FixedScale(gy, vx, vy);
      x = gx + viewx;
      gxt = FixedMul(gx, viewcos);
      nx = gxt - gyt;
      if (nx < 10) nx = 10;
      *screenx = viewwidth - 1;
      *height = (int)(heightnumerator / (uint32_t)nx);
    }
  }
  if (vertical)
    *texture = (y - *texture) & 0xffff;
  else
    *texture = (x - *texture) & 0xffff;
}

/*
========================
=
= TransformSimplePoint
=
========================
*/

static bool TransformSimplePoint(int x, int y, int *screenx, int *height,
                                 int *texture, int vertical) {
  fixed gxt, gyt, nx, ny;
  fixed gxtt, gytt;
  int gx, gy;

  gx = x - viewx;
  gy = y - viewy;

  gxt = FixedMul(gx, viewcos);
  gyt = FixedMul(gy, viewsin);
  nx = gxt - gyt;

  if (nx < MINZ) return false;

  gxtt = FixedMul(gx, viewsin);
  gytt = FixedMul(gy, viewcos);
  ny = gytt + gxtt;

  *screenx = centerx + ((ny * scale) / nx);
  *height = (int)(heightnumerator / (uint32_t)nx);

  if (vertical)
    *texture = (y - *texture) & 0xffff;
  else
    *texture = (x - *texture) & 0xffff;

  return true;
}

/*
========================
=
= TransformPlane
=
========================
*/

bool TransformPlane(int x1, int y1, int x2, int y2, visobj_t *plane) {
  bool result2;
  bool result1;
  bool vertical;
  int txstart, txend;

  vertical = ((x2 - x1) == 0);
  plane->viewx = vertical;
  txstart = plane->texturestart;
  txend = plane->textureend;
  result1 = TransformSimplePoint(x1, y1, &(plane->x1), &(plane->h1),
                                 &(plane->texturestart), vertical);
  result2 = TransformSimplePoint(x2, y2, &(plane->x2), &(plane->h2),
                                 &(plane->textureend), vertical);
  if (result1 == true) {
    if (plane->x1 >= viewwidth) return false;
    if (result2 == false) {
      plane->textureend = txend;
      TransformPoint(x2, y2, &(plane->x2), &(plane->h2), &(plane->textureend),
                     vertical);
    }
  } else {
    if (result2 == false) {
      return false;
    } else {
      if (plane->x2 < 0) return false;
      plane->texturestart = txstart;
      TransformPoint(x1, y1, &(plane->x1), &(plane->h1),
                     &(plane->texturestart), vertical);
    }
  }
  if (plane->x1 < 0) {
    plane->texturestart = txstart;
    TransformPoint(x1, y1, &(plane->x1), &(plane->h1), &(plane->texturestart),
                   vertical);
  }
  if (plane->x2 >= viewwidth) {
    plane->textureend = txend;
    TransformPoint(x2, y2, &(plane->x2), &(plane->h2), &(plane->textureend),
                   vertical);
  }

  plane->viewheight = (plane->h1 + plane->h2) >> 1;

  if ((plane->viewheight >= (2000 << HEIGHTFRACTION)) ||
      (plane->x1 >= viewwidth - 1) || (plane->x2 <= 0))
    return false;

  return true;
}

/*
====================
=
= CalcHeight
=
====================
*/

int CalcHeight(void) {
  fixed gxt, gyt, nx;
  long gx, gy;

  gx = xintercept - viewx;
  gxt = FixedMul(gx, viewcos);

  gy = yintercept - viewy;
  gyt = FixedMul(gy, viewsin);

  nx = gxt - gyt;

  if (nx < mindist) nx = mindist;

  return (int)(heightnumerator / (uint32_t)nx);
}

/*
======================
=
= SortVisibleList (ROTT heap-sorts by viewheight, farthest first)
=
======================
*/

void SortVisibleList(int numvisible, visobj_t *vlist) {
  for (int i = 0; i < numvisible; i++) {
    visobj_t *v = &vlist[i];
    int j = i - 1;
    while (j >= 0 && sortedvislist[j]->viewheight > v->viewheight) {
      sortedvislist[j + 1] = sortedvislist[j];
      j--;
    }
    sortedvislist[j + 1] = v;
  }
}

/*
==========================
=
= SetWallLightLevel
=
==========================
*/

static void SetWallLightLevel(wallcast_t *post) {
  int la = 0;
  int lv;
  int i;

  if (gason) {
    shadingtable = greenmap + (gasindex << 8);
    return;
  }

  switch (post->posttype) {
    case 0:
      la = 0;
      break;
    case 1:
      la = 4;
      break;
    case 2:
      la = (4 - difficulty);
      break;
    case 3:
      la = 3 + (4 - difficulty);
      break;
  }

  if (lightsource) {
    int x, y;
    int intercept;

    x = post->offset >> 7;
    y = post->offset & 0x7f;
    intercept = (post->texture >> 11) & 0x1c;
    lv = (((R_LightAt(x, y) >> intercept) & 0xf) >> 1);
  } else {
    lv = 0;
  }
  if (fulllight) {
    if (fog) {
      i = 16 + minshade - lv + la;
      if (i > maxshade + la) i = maxshade + la;
      shadingtable = colormap + (i << 8);
    } else {
      i = maxshade - 16 - lv + la;
      if (i >= maxshade) i = maxshade;
      if (i < minshade + la) i = minshade + la;
      shadingtable = colormap + (i << 8);
    }
    return;
  }
  if (fog) {
    i = (post->wallheight >> normalshade) + minshade - lv + la;
    if (i > maxshade + la) i = maxshade + la;
    shadingtable = colormap + (i << 8);
  } else {
    i = maxshade - (post->wallheight >> normalshade) - lv + la;
    if (i >= maxshade) i = maxshade;
    if (i < minshade + la) i = minshade + la;
    shadingtable = colormap + (i << 8);
  }
}

/*
====================
=
= DrawWallPost
=
====================
*/

static void DrawWallPost(wallcast_t *post, byte *buf) {
  int ht;
  int topscreen;
  int bottomscreen;
  int texture;
  const byte *src = s_missing_wall;
  const byte *src2;

  texture = post->texture;
  if (post->lump) src = wall_lump(post->lump);
  if (post->alttile != 0) {
    if (post->alttile == -1) {
      ht = maxheight + 32;
      dc_invscale = post->wallheight << (10 - HEIGHTFRACTION);
      dc_texturemid = (pheight << SFRACBITS) + (SFRACUNIT >> 1);
      topscreen = centeryfrac - FixedMul(dc_texturemid, dc_invscale);
      bottomscreen = topscreen + (dc_invscale * ht);
      dc_yh = ((bottomscreen - 1) >> SFRACBITS) + 1;
      if (dc_yh < 0) {
        post->floorclip = -1;
        post->ceilingclip = 0;
      } else if (dc_yh >= viewheight) {
        post->floorclip = viewheight - 1;
        post->ceilingclip = viewheight;
      } else {
        post->floorclip = dc_yh - 1;
        post->ceilingclip = dc_yh;
      }
      return;
    } else {
      ht = nominalheight;
      src2 = wall_lump(post->alttile);
    }
  } else {
    ht = maxheight + 32;
    src2 = src;
  }

  dc_invscale = post->wallheight << (10 - HEIGHTFRACTION);
  dc_texturemid = (pheight << SFRACBITS) + (SFRACUNIT >> 1);
  topscreen = centeryfrac - FixedMul(dc_texturemid, dc_invscale);
  bottomscreen = topscreen + (dc_invscale * ht);
  dc_yl = (topscreen + SFRACUNIT - 1) >> SFRACBITS;
  dc_yh = ((bottomscreen - 1) >> SFRACBITS) + 1;

  if (dc_yl >= viewheight) {
    post->ceilingclip = viewheight;
    post->floorclip = viewheight - 1;
    return;
  } else if (dc_yl < 0) {
    dc_yl = 0;
  }

  dc_iscale = (64 << (16 + HEIGHTFRACTION)) / post->wallheight;

  if (dc_yh < 0) {
    post->floorclip = -1;
    post->ceilingclip = 0;
    goto bottomcheck;
  } else if (dc_yh > viewheight) {
    dc_yh = viewheight;
  }

  post->ceilingclip = dc_yl;
  post->floorclip = dc_yh - 1;
  dc_source = src2 + ((texture >> 4) & 0xfc0);
  R_DrawWallColumn(buf);

bottomcheck:

  if (ht != nominalheight) return;

  dc_texturemid -= (nominalheight << SFRACBITS);
  topscreen = centeryfrac - FixedMul(dc_texturemid, dc_invscale);
  bottomscreen = topscreen + (dc_invscale << 6);
  dc_yl = (topscreen + SFRACUNIT - 1) >> SFRACBITS;
  dc_yh = ((bottomscreen - 1) >> SFRACBITS);

  if (dc_yl >= viewheight)
    return;
  else if (dc_yl < 0)
    dc_yl = 0;
  if (dc_yh < 0)
    return;
  else if (dc_yh > viewheight)
    dc_yh = viewheight;
  post->floorclip = dc_yh - 1;
  dc_source = src + ((texture >> 4) & 0xfc0);
  R_DrawWallColumn(buf);
}

/*
====================
=
= DrawWalls
=
====================
*/

void DrawWalls(void) {
  byte *buf = r_screen;
  wallcast_t *post;

  for (post = &posts[0]; post < &posts[viewwidth]; post++, buf++) {
    SetWallLightLevel(post);
    DrawWallPost(post, buf);
  }
}

/*
========================
=
= InterpolateWall
=
========================
*/

static void InterpolateWall(visobj_t *plane) {
  int d1, d2;
  int top;
  int topinc;
  int bot;
  int botinc;
  int i;
  int texture;
  int dh;
  int dx;
  int height;

  dx = (plane->x2 - plane->x1 + 1);
  if (plane->h1 <= 0 || plane->h2 <= 0 || dx == 0) return;
  d1 = (1 << (16 + HEIGHTFRACTION)) / plane->h1;
  d2 = (1 << (16 + HEIGHTFRACTION)) / plane->h2;
  dh = (((plane->h2 - plane->h1) << DHEIGHTFRACTION) +
        (1 << (DHEIGHTFRACTION - 1))) /
       dx;
  top = 0;
  topinc = FixedMulShift(d1, plane->textureend - plane->texturestart, 4);
  bot = d2 * dx;
  botinc = d1 - d2;
  height = plane->h1 << DHEIGHTFRACTION;
  if (plane->x1 >= viewwidth) return;
  for (i = plane->x1; i <= plane->x2; i++) {
    if ((i >= 0 && i < viewwidth) &&
        (posts[i].wallheight <= (height >> DHEIGHTFRACTION))) {
      if (bot) {
        texture = ((top / bot) + (plane->texturestart >> 4)) & 0xfc0;
        posts[i].texture = texture << 4;
        posts[i].lump = plane->shapenum;
        posts[i].alttile = plane->altshapenum;
        posts[i].posttype = plane->viewx;
        posts[i].offset = plane->shapesize;
        posts[i].wallheight = height >> DHEIGHTFRACTION;
      }
    }
    top += topinc;
    bot += botinc;
    height += dh;
  }
}

/*
========================
=
= InterpolateDoor
=
========================
*/

void InterpolateDoor(visobj_t *plane) {
  int d1, d2;
  int top;
  int topinc;
  int bot;
  int botinc;
  int i;
  int texture;
  int dh;
  int dx;
  int height;
  int bottomscreen;
  const byte *shape;
  const byte *shape2;
  byte *buf;
  const patch_t *p;

  dx = (plane->x2 - plane->x1 + 1);
  if (plane->h1 <= 0 || plane->h2 <= 0 || dx == 0) return;
  shape = R_Lump(plane->shapenum);
  shape2 = wall_lump(plane->altshapenum);
  if (!shape) return;
  p = (const patch_t *)shape;
  d1 = (1 << (16 + HEIGHTFRACTION)) / plane->h1;
  d2 = (1 << (16 + HEIGHTFRACTION)) / plane->h2;
  dh = (((plane->h2 - plane->h1) << DHEIGHTFRACTION) +
        (1 << (DHEIGHTFRACTION - 1))) /
       dx;
  topinc = FixedMulShift(d1, plane->textureend - plane->texturestart, 4);
  botinc = d1 - d2;
  if (plane->x1 >= viewwidth) return;

  top = 0;
  bot = (d2 * dx);
  height = (plane->h1 << DHEIGHTFRACTION);
  buf = r_screen + (plane->x1);

  for (i = plane->x1; i <= plane->x2; i++, buf++) {
    if ((i >= 0 && i < viewwidth) && (bot != 0) &&
        (posts[i].wallheight <= (height >> DHEIGHTFRACTION))) {
      dc_invscale = height >> (HEIGHTFRACTION + DHEIGHTFRACTION - 10);
      if (dc_invscale <= 0) goto next;
      dc_iscale = (int)(0xffffffffu / (unsigned)dc_invscale);
      dc_texturemid = ((pheight - nominalheight + p->topoffset) << SFRACBITS) +
                      (SFRACUNIT >> 1);
      sprtopoffset = centeryfrac - FixedMul(dc_texturemid, dc_invscale);

      texture = ((top / bot) + (plane->texturestart >> 4)) >> 6;
      if ((unsigned)texture < (unsigned)p->width) {
        SetLightLevel(height >> DHEIGHTFRACTION);
        ScaleMaskedPost(p->collumnofs[texture] + shape, buf);

        if (levelheight > 1) {
          sprtopoffset -= (dc_invscale << 6) * (levelheight - 1);
          bottomscreen = sprtopoffset + (dc_invscale * nominalheight);
          dc_yl = (sprtopoffset + SFRACUNIT - 1) >> SFRACBITS;
          dc_yh = ((bottomscreen - 1) >> SFRACBITS) + 1;
          if (dc_yl < viewheight) {
            if (dc_yl < 0) dc_yl = 0;
            if (dc_yh > viewheight) dc_yh = viewheight;
            dc_source = shape2 + ((texture << 6) & 0xfc0);
            R_DrawWallColumn(buf);
          }
        }
      }
    }
  next:
    top += topinc;
    bot += botinc;
    height += dh;
  }
}

/*
========================
=
= InterpolateMaskedWall
=
========================
*/

void InterpolateMaskedWall(visobj_t *plane) {
  int d1, d2;
  int top;
  int topinc;
  int bot;
  int botinc;
  int i;
  int j;
  int texture;
  int dh;
  int dx;
  int height;
  const byte *shape = NULL;
  const byte *shape2 = NULL;
  const byte *shape3 = NULL;
  byte *buf;
  const transpatch_t *p = NULL;
  const patch_t *p2 = NULL;
  const patch_t *p3 = NULL;
  bool drawbottom, drawmiddle, drawtop;
  int topoffset = 0;
  int width = 0x7fff;

  dx = (plane->x2 - plane->x1 + 1);
  if (plane->h1 <= 0 || plane->h2 <= 0 || dx == 0) return;
  drawmiddle = false;
  if (plane->altshapenum >= 0 && (shape2 = R_Lump(plane->altshapenum))) {
    drawmiddle = true;
    p2 = (const patch_t *)shape2;
    topoffset = p2->topoffset;
    if (p2->width < width) width = p2->width;
  }
  drawtop = false;
  if (plane->viewx >= 0 && (shape3 = R_Lump(plane->viewx))) {
    drawtop = true;
    p3 = (const patch_t *)shape3;
    topoffset = p3->topoffset;
    if (p3->width < width) width = p3->width;
  }
  drawbottom = false;
  if (plane->shapenum >= 0 && (shape = R_Lump(plane->shapenum))) {
    drawbottom = true;
    p = (const transpatch_t *)shape;
    topoffset = p->topoffset;
    if (p->width < width) width = p->width;
  }
  if (!drawbottom && !drawmiddle && !drawtop) return;

  d1 = (1 << (16 + HEIGHTFRACTION)) / plane->h1;
  d2 = (1 << (16 + HEIGHTFRACTION)) / plane->h2;
  dh = (((plane->h2 - plane->h1) << DHEIGHTFRACTION) +
        (1 << (DHEIGHTFRACTION - 1))) /
       dx;
  topinc = FixedMulShift(d1, plane->textureend - plane->texturestart, 4);
  botinc = d1 - d2;
  if (plane->x1 >= viewwidth) return;

  top = 0;
  bot = (d2 * dx);
  height = (plane->h1 << DHEIGHTFRACTION);
  buf = r_screen + (plane->x1);
  for (i = plane->x1; i <= plane->x2; i++, buf++) {
    if ((i >= 0 && i < viewwidth) && (bot != 0) &&
        (posts[i].wallheight <= (height >> DHEIGHTFRACTION))) {
      dc_invscale = height >> (HEIGHTFRACTION + DHEIGHTFRACTION - 10);
      if (dc_invscale <= 0) goto next;
      dc_iscale = (int)(0xffffffffu / (unsigned)dc_invscale);
      dc_texturemid = ((pheight - nominalheight + topoffset) << SFRACBITS) +
                      (SFRACUNIT >> 1);
      sprtopoffset = centeryfrac - FixedMul(dc_texturemid, dc_invscale);

      texture = ((top / bot) + (plane->texturestart >> 4)) >> 6;
      if ((unsigned)texture < (unsigned)width) {
        SetLightLevel(height >> DHEIGHTFRACTION);
        if (drawbottom == true)
          ScaleTransparentPost(p->collumnofs[texture] + shape, buf,
                               (p->translevel + 8));
        for (j = 0; j < levelheight - 2; j++) {
          sprtopoffset -= (dc_invscale << 6);
          dc_texturemid += (1 << 22);
          if (drawmiddle == true)
            ScaleMaskedPost(p2->collumnofs[texture] + shape2, buf);
        }
        if (levelheight > 1) {
          sprtopoffset -= (dc_invscale << 6);
          dc_texturemid += (1 << 22);
          if (drawtop == true)
            ScaleMaskedPost(p3->collumnofs[texture] + shape3, buf);
        }
      }
    }
  next:
    top += topinc;
    bot += botinc;
    height += dh;
  }
}

/*
====================
=
= MakeWideDoorVisible
=
====================
*/

void MakeWideDoorVisible(int doornum) {
  int dx, dy;
  const r_door_t *dr;
  int tx, ty;

  dr = &doorobjlist[doornum];

  dx = 0;
  dy = 0;
  if (dr->vertical)
    dy = 1;
  else
    dx = 1;
  SET_SPOTVIS(dr->tilex, dr->tiley);
  tx = dr->tilex + dx;
  ty = dr->tiley + dy;
  while (tx < MAPSIZE && ty < MAPSIZE && M_ISDOOR(tx, ty)) {
    const int num = tilemap[tx][ty] & 0x3ff;
    if (!(doorobjlist[num].flags & DF_MULTI)) break;
    SET_SPOTVIS(tx, ty);
    tx += dx;
    ty += dy;
  }
  tx = dr->tilex - dx;
  ty = dr->tiley - dy;
  while (tx >= 0 && ty >= 0 && M_ISDOOR(tx, ty)) {
    const int num = tilemap[tx][ty] & 0x3ff;
    if (!(doorobjlist[num].flags & DF_MULTI)) break;
    SET_SPOTVIS(tx, ty);
    tx -= dx;
    ty -= dy;
  }
}

/*
====================
=
= TransformDoors
=
====================
*/

void TransformDoors(void) {
  int i;
  int numvisible;
  bool result;
  int gx, gy;
  visobj_t visdoorlist[MAXVISIBLEDOORS], *doorptr;

  doorptr = &visdoorlist[0];

  for (i = 0; i < doornum; i++) {
    const r_door_t *d = &doorobjlist[i];
    if (SPOTVIS(d->tilex, d->tiley)) {
      R_MarkSeen(d->tilex, d->tiley);
      doorptr->texturestart = 0;
      doorptr->textureend = 0;
      if (d->vertical) {
        gx = (d->tilex << 16) + 0x8000;
        gy = (d->tiley << 16);
        if (viewx < gx)
          result = TransformPlane(gx, gy, gx, gy + 0xffff, doorptr);
        else
          result = TransformPlane(gx, gy + 0xffff, gx, gy, doorptr);
      } else {
        gx = (d->tilex << 16);
        gy = (d->tiley << 16) + 0x8000;
        if (viewy < gy)
          result = TransformPlane(gx + 0xffff, gy, gx, gy, doorptr);
        else
          result = TransformPlane(gx, gy, gx + 0xffff, gy, doorptr);
      }
      if (result == true) {
        doorptr->viewx = 0;
        doorptr->shapenum = d->texture;
        doorptr->altshapenum = d->alttexture;
        if (d->texture == d->basetexture) {
          doorptr->shapesize = (d->tilex << 7) + d->tiley;
          if (doorptr < &visdoorlist[MAXVISIBLEDOORS - 1]) doorptr++;
        } else {
          doorptr->shapesize = 3;
          memcpy(visptr, doorptr, sizeof(visobj_t));
          if (visptr < &vislist[MAXVISIBLE - 1]) visptr++;
        }
      }
    }
  }
  numvisible = doorptr - &visdoorlist[0];
  if (!numvisible) return;
  SortVisibleList(numvisible, &visdoorlist[0]);
  for (i = 0; i < numvisible; i++) {
    InterpolateWall(sortedvislist[i]);
  }
}

/*
====================
=
= TransformPushWalls
=
====================
*/

void TransformPushWalls(void) {
  int i;
  int gx, gy;
  visobj_t *savedptr;
  int numvisible;
  bool result;

  savedptr = visptr;
  for (i = 0; i < pwallnum; i++) {
    const r_pwall_t *pw = &pwallobjlist[i];
    const int tx = pw->x >> 16;
    const int ty = pw->y >> 16;
    if ((pw->action == pw_pushed) || (pw->action == pw_npushed)) continue;
    if (tx < 1 || ty < 1 || tx >= MAPSIZE - 1 || ty >= MAPSIZE - 1) continue;
    if (SPOTVIS(tx, ty) || SPOTVIS(tx, ty - 1) || SPOTVIS(tx, ty + 1) ||
        SPOTVIS(tx - 1, ty) || SPOTVIS(tx + 1, ty)) {
      gx = pw->x;
      gy = pw->y;
      R_MarkSeen(gx >> 16, gy >> 16);
      if (viewx < gx) {
        if (viewy < gy) {
          visptr->texturestart = (gx - 0x8000) & 0xffff;
          visptr->textureend = visptr->texturestart;
          result = TransformPlane(gx + 0x7fff, gy - 0x8000, gx - 0x8000,
                                  gy - 0x8000, visptr);
          visptr->texturestart ^= 0xffff;
          visptr->textureend ^= 0xffff;
          visptr->shapenum = pw->texture;
          visptr->shapesize = ((pw->x >> 16) << 7) + (pw->y >> 16);
          visptr->viewx += 2;
          if ((visptr < &vislist[MAXVISIBLE - 1]) && (result == true)) visptr++;
          visptr->texturestart = (gy - 0x8000) & 0xffff;
          visptr->textureend = visptr->texturestart;
          result = TransformPlane(gx - 0x8000, gy - 0x8000, gx - 0x8000,
                                  gy + 0x7fff, visptr);
        } else {
          visptr->texturestart = (gy - 0x8000) & 0xffff;
          visptr->textureend = visptr->texturestart;
          result = TransformPlane(gx - 0x8000, gy - 0x8000, gx - 0x8000,
                                  gy + 0x7fff, visptr);
          visptr->shapenum = pw->texture;
          visptr->shapesize = ((pw->x >> 16) << 7) + (pw->y >> 16);
          visptr->viewx += 2;
          if ((visptr < &vislist[MAXVISIBLE - 1]) && (result == true)) visptr++;
          visptr->texturestart = (gx - 0x8000) & 0xffff;
          visptr->textureend = visptr->texturestart;
          result = TransformPlane(gx - 0x8000, gy + 0x7fff, gx + 0x7fff,
                                  gy + 0x7fff, visptr);
        }
      } else {
        if (viewy < gy) {
          visptr->texturestart = (gy - 0x8000) & 0xffff;
          visptr->textureend = visptr->texturestart;
          result = TransformPlane(gx + 0x7fff, gy + 0x7fff, gx + 0x7fff,
                                  gy - 0x8000, visptr);
          visptr->texturestart ^= 0xffff;
          visptr->textureend ^= 0xffff;
          visptr->shapenum = pw->texture;
          visptr->shapesize = ((pw->x >> 16) << 7) + (pw->y >> 16);
          visptr->viewx += 2;
          if ((visptr < &vislist[MAXVISIBLE - 1]) && (result == true)) visptr++;
          visptr->texturestart = (gx - 0x8000) & 0xffff;
          visptr->textureend = visptr->texturestart;
          result = TransformPlane(gx + 0x7fff, gy - 0x8000, gx - 0x8000,
                                  gy - 0x8000, visptr);
          visptr->texturestart ^= 0xffff;
          visptr->textureend ^= 0xffff;
        } else {
          visptr->texturestart = (gx - 0x8000) & 0xffff;
          visptr->textureend = visptr->texturestart;
          result = TransformPlane(gx - 0x8000, gy + 0x7fff, gx + 0x7fff,
                                  gy + 0x7fff, visptr);
          visptr->shapenum = pw->texture;
          visptr->shapesize = ((pw->x >> 16) << 7) + (pw->y >> 16);
          visptr->viewx += 2;
          if ((visptr < &vislist[MAXVISIBLE - 1]) && (result == true)) visptr++;
          visptr->texturestart = (gy - 0x8000) & 0xffff;
          visptr->textureend = visptr->texturestart;
          result = TransformPlane(gx + 0x7fff, gy + 0x7fff, gx + 0x7fff,
                                  gy - 0x8000, visptr);
          visptr->texturestart ^= 0xffff;
          visptr->textureend ^= 0xffff;
        }
      }
      visptr->viewx += 2;
      visptr->shapenum = pw->texture;
      visptr->shapesize = ((pw->x >> 16) << 7) + (pw->y >> 16);
      if ((visptr < &vislist[MAXVISIBLE - 1]) && (result == true)) visptr++;
    }
  }

  numvisible = visptr - savedptr;
  if (!numvisible) return;
  SortVisibleList(numvisible, savedptr);
  for (i = 0; i < numvisible; i++) {
    if (sortedvislist[i]->shapenum & 0x1000)
      sortedvislist[i]->shapenum =
          animwalls[sortedvislist[i]->shapenum & 0x3ff];
    sortedvislist[i]->altshapenum = 0;
    InterpolateWall(sortedvislist[i]);
  }
  visptr = savedptr;
}
