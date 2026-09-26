/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * File: r_things.c
 * Description: Masked walls, sprites, the weapon and screen overlays,
 *              from atarist-rott's rt_draw.c (DrawScaleds,
 *              SetSpriteLightLevel, SetColorLightLevel, DrawPlayerWeapon,
 *              the tail of ThreeDRefresh).
 *
 * The ST walks its statics and actors and sends every object that might
 * be visible, with the shape already rotated and height-flipped (those
 * need game state). Here each one is re-checked against this frame's own
 * spotvis -- the ST chose them from the previous frame's -- then
 * transformed, lit, sorted and drawn exactly as DrawScaleds does.
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

r_obj_t r_objs[R_MAX_OBJS];
int r_numobjs;
r_weapon_t r_weapon[2];
int r_numweapon;
r_overlay_t r_overlay;

/* ------------------------------------------------------------------ */
/* Frame records                                                        */
/* ------------------------------------------------------------------ */

void R_FrameReset(void) {
  r_numobjs = 0;
  r_numweapon = 0;
  memset(&r_overlay, 0, sizeof(r_overlay));
}

static void set_messages(const uint16_t *w, unsigned nwords) {
  unsigned i = 0;
  r_nummessages = 0;
  while (i < nwords && r_nummessages < R_MAX_MSG_LINES) {
    unsigned len = w[i++];
    const unsigned words = (len + 1u) >> 1;
    if (i + words > nwords) break;
    if (len > R_MAX_MSG_CHARS - 1) len = R_MAX_MSG_CHARS - 1;
    md_get_bytes((uint8_t *)r_messages[r_nummessages], w + i, len);
    r_messages[r_nummessages][len] = 0;
    r_nummessages++;
    i += words;
  }
}

bool R_FrameRecord(unsigned type, unsigned count, const uint16_t *it) {
  unsigned i;
  switch (type) {
    case MD_REC_VIEW:
      if (count != 1) return false;
      R_SetView(it);
      return true;

    case MD_REC_OBJS:
      for (i = 0; i < count && r_numobjs < R_MAX_OBJS;
           i++, it += MD_OBJ_WORDS) {
        r_obj_t *o = &r_objs[r_numobjs++];
        o->x = it[0];
        o->y = it[1];
        o->z = (int16_t)it[2];
        o->shapenum = it[3];
        o->flags = it[4];
        o->extra = it[5];
      }
      return true;

    case MD_REC_WEAPON:
      for (i = 0; i < count && r_numweapon < 2; i++, it += MD_WEAPON_WORDS) {
        r_weapon_t *wp = &r_weapon[r_numweapon++];
        wp->xoff = (int16_t)it[0];
        wp->yoff = (int16_t)it[1];
        wp->shapenum = it[2];
        wp->mode = (int16_t)it[3];
      }
      return true;

    case MD_REC_OVERLAY:
      if (count != 1) return false;
      r_overlay.flags = it[0];
      r_overlay.gmasklump = it[1];
      r_overlay.eyex = (int16_t)it[2];
      r_overlay.eyey = (int16_t)it[3];
      r_overlay.eyeshape = it[4];
      r_overlay.netlump = it[5];
      return true;

    case MD_REC_MSGS:
      set_messages(it, count);
      return true;

    default:
      return false;
  }
}

/* ------------------------------------------------------------------ */
/* Sprite lighting                                                      */
/* ------------------------------------------------------------------ */

static void SetSpriteLightLevel(int x, int y, visobj_t *sprite, bool eastwest,
                                bool fullbright) {
  int i;
  int lv;
  int intercept;

  if (gason) {
    sprite->colormap = greenmap + (gasindex << 8);
    return;
  }

  if (fulllight || fullbright) {
    sprite->colormap = colormap + (1 << 12);
    return;
  }

  if (fog) {
    i = (sprite->viewheight >> normalshade) + minshade;
    if (i > maxshade) i = maxshade;
    sprite->colormap = colormap + (i << 8);
  } else {
    if (lightsource) {
      if (eastwest)
        intercept = (x >> 11) & 0x1c;
      else
        intercept = (y >> 11) & 0x1c;

      lv = (((R_LightAt(x >> 16, y >> 16) >> intercept) & 0xf) >> 1);
      i = maxshade - (sprite->viewheight >> normalshade) - lv;
      if (i < minshade) i = minshade;
      sprite->colormap = colormap + (i << 8);
    } else {
      i = maxshade - (sprite->viewheight >> normalshade);
      if (i < minshade) i = minshade;
      sprite->colormap = colormap + (i << 8);
    }
  }
}

static void SetColorLightLevel(int x, int y, visobj_t *sprite, bool eastwest,
                               int color, bool fullbright) {
  int i;
  int lv;
  int intercept;
  int height;
  const byte *map;

  height = sprite->viewheight << 1;
  if (color < 0 || color >= MAXPLAYERCOLORS) color = 0;
  map = R_PlayerMap(color);
  if (gason) {
    sprite->colormap = greenmap + (gasindex << 8);
    return;
  }

  if ((fulllight) || (fullbright)) {
    sprite->colormap = map + (1 << 12);
    return;
  }

  if (fog) {
    i = (height >> normalshade) + minshade;
    if (i > maxshade) i = maxshade;
    sprite->colormap = map + (i << 8);
  } else {
    if (lightsource) {
      if (eastwest)
        intercept = (x >> 11) & 0x1c;
      else
        intercept = (y >> 11) & 0x1c;

      lv = (((R_LightAt(x >> 16, y >> 16) >> intercept) & 0xf) >> 1);
      i = maxshade - (height >> normalshade) - lv;
      if (i < minshade) i = minshade;
      sprite->colormap = map + (i << 8);
    } else {
      i = maxshade - (height >> normalshade);
      if (i < minshade) i = minshade;
      sprite->colormap = map + (i << 8);
    }
  }
}

/* ------------------------------------------------------------------ */
/* DrawScaleds                                                          */
/* ------------------------------------------------------------------ */

static bool near_visible(int tx, int ty) {
  if (tx < 1 || ty < 1 || tx >= MAPSIZE - 1 || ty >= MAPSIZE - 1) return false;
  /* ROTT's visspot +-1, +-127..129: the tile and its eight neighbours. */
  for (int dx = -1; dx <= 1; dx++) {
    for (int dy = -1; dy <= 1; dy++) {
      if (SPOTVIS(tx + dx, ty + dy)) return true;
    }
  }
  return false;
}

void DrawScaleds(void) {
  int i, numvisible;
  int gx, gy;
  bool result;

  /* place maskwall objects */
  for (i = 0; i < maskednum; i++) {
    const r_mwall_t *tmwall = &maskobjlist[i];
    if (!tmwall->active) continue;
    if (!SPOTVIS(tmwall->tilex, tmwall->tiley)) continue;
    R_MarkSeen(tmwall->tilex, tmwall->tiley);
    if (tmwall->vertical) {
      gx = (tmwall->tilex << 16) + 0x8000;
      gy = (tmwall->tiley << 16);
      visptr->texturestart = 0;
      visptr->textureend = 0;
      if (viewx < gx)
        result = TransformPlane(gx, gy, gx, gy + 0xffff, visptr);
      else
        result = TransformPlane(gx, gy + 0xffff, gx, gy, visptr);
    } else {
      gx = (tmwall->tilex << 16);
      gy = (tmwall->tiley << 16) + 0x8000;
      visptr->texturestart = 0;
      visptr->textureend = 0;
      if (viewy < gy)
        result = TransformPlane(gx + 0xffff, gy, gx, gy, visptr);
      else
        result = TransformPlane(gx, gy, gx + 0xffff, gy, visptr);
    }
    visptr->shapenum = tmwall->bottomtexture;
    visptr->altshapenum = tmwall->midtexture;
    visptr->viewx = tmwall->toptexture;
    visptr->shapesize = 2;
    if ((tmwall->flags & MW_TOPFLIPPING) && (nonbobpheight > 64)) {
      visptr->viewx++;
    } else if ((tmwall->flags & MW_BOTTOMFLIPPING) &&
               (nonbobpheight > maxheight - 32)) {
      visptr->shapenum++;
    }
    if ((visptr < &vislist[MAXVISIBLE - 1]) && (result == true)) visptr++;
  }

  /* place static and active objects, as sent by the ST */
  for (i = 0; i < r_numobjs; i++) {
    const r_obj_t *o = &r_objs[i];
    const int x = (int)o->x << 8;
    const int y = (int)o->y << 8;
    const unsigned kind = o->flags & MD_OF_KIND_MASK;
    const bool eastwest = (o->flags & MD_OF_EASTWEST) != 0;
    const bool fullbright = (o->flags & MD_OF_FULLBRIGHT) != 0;

    if (!near_visible(x >> 16, y >> 16)) continue;

    visptr->shapenum = o->shapenum;
    result = TransformObject(x, y, &(visptr->viewx), &(visptr->viewheight));
    if ((result == false) ||
        (visptr->viewheight < (1 << (HEIGHTFRACTION + 2))))
      continue; /* too close to the object */

    switch (kind) {
      case MD_OF_TRANSLUCENT:
        visptr->shapesize = 1;
        visptr->h2 = o->extra;
        SetSpriteLightLevel(x, y, visptr, eastwest, fullbright);
        break;
      case MD_OF_SOLID:
        visptr->shapesize = 4;
        visptr->h2 = o->extra;
        break;
      case MD_OF_COLORED:
        visptr->shapesize = 0;
        SetColorLightLevel(x, y, visptr, eastwest, o->extra, fullbright);
        break;
      case MD_OF_REDMAP:
        visptr->shapesize = 0;
        visptr->colormap = redmap + ((o->extra - 1) << 8);
        break;
      default:
        visptr->shapesize = 0;
        SetSpriteLightLevel(x, y, visptr, eastwest, fullbright);
        break;
    }

    visptr->h1 = pheight - o->z;

    if (visptr < &vislist[MAXVISIBLE - 1]) visptr++;
  }

  /* draw from back to front */
  numvisible = visptr - &vislist[0];
  if (!numvisible) return;
  SortVisibleList(numvisible, &vislist[0]);
  for (i = 0; i < numvisible; i++) {
    switch (sortedvislist[i]->shapesize) {
      case 4:
        ScaleSolidShape(sortedvislist[i]);
        break;
      case 3:
        InterpolateDoor(sortedvislist[i]);
        break;
      case 2:
        InterpolateMaskedWall(sortedvislist[i]);
        break;
      case 1:
        ScaleTransparentShape(sortedvislist[i]);
        break;
      default:
        ScaleShape(sortedvislist[i]);
        break;
    }
  }
}

/* ------------------------------------------------------------------ */
/* Weapon and overlays (ThreeDRefresh after DrawScaleds)                */
/* ------------------------------------------------------------------ */

void DrawPlayerWeapon(void) {
  if (r_overlay.flags & MD_OV_NET) DrawScreenSizedSprite(r_overlay.netlump);

  /* DrawPlayerWeapon: the bob widens or narrows the weapon by
   * FixedMul(weaponbobx << 9, weaponscale); the ST says which way. */
  for (int i = 0; i < r_numweapon; i++) {
    const r_weapon_t *wp = &r_weapon[i];
    const int temp = weaponscale;
    const int delta = FixedMul((weaponbobx << 9), weaponscale);
    if (wp->mode > 0) weaponscale += delta;
    if (wp->mode < 0) weaponscale -= delta;
    ScaleWeapon(wp->xoff, wp->yoff, wp->shapenum);
    weaponscale = temp;
  }
}

void DrawOverlays(void) {
  if (r_overlay.flags & MD_OV_EYE)
    DrawScreenSprite(r_overlay.eyex, r_overlay.eyey, r_overlay.eyeshape);
  if (r_overlay.flags & MD_OV_GASMASK)
    DrawScreenSizedSprite(r_overlay.gmasklump);
}
