/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * File: r_main.c
 * Description: MD/ROTT renderer: level and view setup, and the frame.
 *              From rt_view.c (SetViewSize, SetViewDelta, CalcProjection,
 *              LoadColorMap), rt_draw.c (WallRefresh, ThreeDRefresh) and
 *              rt_floor.c (SetPlaneViewSize).
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

#include "atari_tables.h"
#include "r_local.h"

/* ------------------------------------------------------------------ */
/* Level                                                                */
/* ------------------------------------------------------------------ */

int maxheight, nominalheight, levelheight;
int wstart, shapestart, shapestop, gunsstart, elevatorstart;
int sky, centerskypost, skytoplump, skybottomlump;
int floorlump, ceilinglump, fontlump;
int difficulty;
static byte s_colormap[32 * 256];
byte *colormap = s_colormap;
const byte *redmap;
const byte *greenmap;
static int s_playmaps;

/* ------------------------------------------------------------------ */
/* View                                                                 */
/* ------------------------------------------------------------------ */

fixed viewx, viewy;
int viewangle;
fixed viewsin, viewcos;
int c_startx, c_starty;
int pheight, nonbobpheight;
int viewwidth, viewheight;
int centerx, centery, centeryfrac;
fixed scale;
uint32_t heightnumerator;
int weaponscale;
int weaponbobx, weaponboby;
int16_t pixelangle[MD_VIEW_MAX_W];
int playerz;
int tictime;

int fulllight, fog, lightsource, lightning, lightninglevel;
int minshade, maxshade, normalshade, basemaxshade;
int gason, gasindex, transparentlevel, shrooms;

const int32_t *sintable = (const int32_t *)atari_sintable;
const int32_t *costable = (const int32_t *)atari_sintable + (FINEANGLES / 4);

byte *r_screen;
int r_view_screen_x, r_view_screen_y;

static int s_focalwidth = -1;
static int s_projwidth = -1;
static int s_yzangle;

/* ------------------------------------------------------------------ */

bool R_BeginLevel(const uint16_t *lv, unsigned nwords) {
  if (nwords < MD_LV_WORDS) return false;

  sky = lv[MD_LV_SKY];
  skytoplump = lv[MD_LV_SKYTOP];
  skybottomlump = lv[MD_LV_SKYBOTTOM];
  centerskypost = (int16_t)lv[MD_LV_SKYCENTER];
  floorlump = lv[MD_LV_FLOOR];
  ceilinglump = lv[MD_LV_CEILING];
  lightning = (lv[MD_LV_FLAGS] & MD_LVF_LIGHTNING) != 0;
  maxheight = (int16_t)lv[MD_LV_MAXHEIGHT];
  nominalheight = (int16_t)lv[MD_LV_NOMINALHEIGHT];
  levelheight = (int16_t)lv[MD_LV_LEVELHEIGHT];
  wstart = lv[MD_LV_WSTART];
  shapestart = lv[MD_LV_SHAPESTART];
  shapestop = lv[MD_LV_SHAPESTOP];
  gunsstart = lv[MD_LV_GUNSSTART];
  elevatorstart = lv[MD_LV_ELEVSTART];
  fontlump = lv[MD_LV_FONT];
  difficulty = lv[MD_LV_DIFFICULTY];

  /* LoadColorMap: a RAM copy, because the fire colours are patched. */
  const byte *cm = R_Lump(lv[MD_LV_COLORMAP]);
  if (!cm) return false;
  memcpy(s_colormap, cm, sizeof(s_colormap));
  for (int i = 31; i >= 16; i--) {
    for (int j = 0xea; j < 0xf9; j++) {
      s_colormap[i * 256 + j] = s_colormap[(((i - 16) / 4 + 16)) * 256 + j];
    }
  }
  redmap = R_LumpFixed(lv[MD_LV_SPECMAPS] + 1);
  if (!redmap) redmap = colormap;
  greenmap = redmap + (16 * 256);
  s_playmaps = lv[MD_LV_PLAYMAPS];
  s_projwidth = -1;
  return true;
}

/* A player's uniform colours: only other players use them, so they are
 * fetched when drawn rather than kept for the level. */
const byte *R_PlayerMap(int color) {
  const byte *map = NULL;
  if (color >= 0 && color < MAXPLAYERCOLORS) {
    map = R_Lump(s_playmaps + 1 + color);
  }
  return map ? map : colormap;
}

/* rt_view.c SetViewDelta + CalcProjection, for this frame's width. */
static void setup_projection(int width, int height, int focalwidth) {
  viewwidth = width;
  viewheight = height;
  centerx = viewwidth >> 1;

  scale = (centerx * focalwidth) / 160;
  heightnumerator = (uint32_t)(((focalwidth / 10) * centerx * 4096)
                               << HEIGHTFRACTION);

  int length = ATARI_PANGLE_LEN;
  int frac = ((length * 65536 / centerx)) >> 1;
  for (int i = 0; i < centerx; i++) {
    const int intang = atari_pangle[frac >> 16];
    pixelangle[centerx - 1 - i] = (int16_t)intang;
    pixelangle[centerx + i] = (int16_t)-intang;
    frac += (length * 65536 / centerx);
  }

  /* SetViewSize: the weapon is scaled to the view, capped at 168. */
  int h = viewheight > 168 ? 168 : viewheight;
  weaponscale = (h << 16) / 168;

  s_projwidth = width;
  s_focalwidth = focalwidth;
}

void R_SetView(const uint16_t *v) {
  const int width = v[MD_V_WIDTH];
  const int height = v[MD_V_HEIGHT];
  const int focal = (int16_t)v[MD_V_FOCAL];

  r_view_screen_x = v[MD_V_SCREENX];
  r_view_screen_y = v[MD_V_SCREENY];

  if (width != s_projwidth || height != viewheight || focal != s_focalwidth) {
    setup_projection(width, height, focal);
  }

  viewx = (fixed)md_get32(v + MD_V_VIEWX);
  viewy = (fixed)md_get32(v + MD_V_VIEWY);
  viewangle = v[MD_V_ANGLE] & (FINEANGLES - 1);
  pheight = (int16_t)v[MD_V_PHEIGHT];
  nonbobpheight = (int16_t)v[MD_V_NONBOB];
  s_yzangle = v[MD_V_YZANGLE] & (FINEANGLES - 1);
  weaponbobx = (int16_t)v[MD_V_BOBX];
  weaponboby = (int16_t)v[MD_V_BOBY];

  const unsigned f = v[MD_V_FLAGS];
  fulllight = (f & MD_VF_FULLLIGHT) != 0;
  fog = (f & MD_VF_FOG) != 0;
  lightsource = (f & MD_VF_LIGHTSOURCE) != 0;
  gason = (f & MD_VF_GASON) != 0;
  shrooms = (f & MD_VF_SHROOMS) != 0;
  if (!(f & MD_VF_LIGHTNING)) lightninglevel = 0;
  minshade = (int16_t)v[MD_V_MINSHADE];
  maxshade = (int16_t)v[MD_V_MAXSHADE];
  normalshade = (int16_t)v[MD_V_NORMALSHADE];
  basemaxshade = (int16_t)v[MD_V_BASEMAXSHADE];
  if (f & MD_VF_LIGHTNING) lightninglevel = (int16_t)v[MD_V_LIGHTNINGLVL];
  gasindex = v[MD_V_GASINDEX];
  transparentlevel = (int16_t)v[MD_V_TRANSLEVEL];
  tictime = v[MD_V_TICS];
  playerz = (int16_t)v[MD_V_PLAYERZ];
  r_bordercolor = v[MD_V_BORDER];
  r_paused = (f & MD_VF_PAUSED) != 0;
}

/* The second half of rt_draw.c WallRefresh, after the ST worked out the
 * view position: the YZ angle and the caster's start vectors. */
static void setup_view(void) {
  /* SetViewSize's yzangleconverter for this view's height. */
  const int yzangleconverter = (0xaf85 * viewheight) / 200;

  centery = viewheight >> 1;
  if (s_yzangle > ANG180) {
    centery -= FixedMul(FINEANGLES - s_yzangle, yzangleconverter);
  } else {
    centery += FixedMul(s_yzangle, yzangleconverter);
  }
  centeryfrac = (centery << 16);

  if (pheight < 1) {
    pheight = 1;
  } else if (pheight > maxheight + 30) {
    pheight = maxheight + 30;
  }
  if (nonbobpheight < 1) {
    nonbobpheight = 1;
  } else if (nonbobpheight > maxheight + 30) {
    nonbobpheight = maxheight + 30;
  }

  viewsin = sintable[viewangle];
  viewcos = costable[viewangle];
  c_startx = (scale * viewcos) - (centerx * viewsin);
  c_starty = (-scale * viewsin) - (centerx * viewcos);
}

void R_RenderFrame(byte *screen) {
  r_screen = screen;
  R_ClearFrameBits();
  setup_view();

  /* ThreeDRefresh, with WallRefresh's tail inlined. */
  visptr = &vislist[0];
  SET_SPOTVIS(viewx >> 16, viewy >> 16);
  Refresh();
  TransformPushWalls();
  TransformDoors();
  DrawWalls();
  DrawPlanes();
  DrawScaleds();
  DrawPlayerWeapon();
  DrawOverlays();
  DrawBorder();
  DrawMessages();
  if (r_paused) DrawPause();
  R_FinishFrameBits();
}
