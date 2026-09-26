/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * File: r_cast.c
 * Description: The ray caster, from atarist-rott's engine.c: Refresh,
 *              InitialCast, Cast, HitWall, Interpolate. The Atari cut
 *              (ATARI_MAX_RAY_STEPS and the fake far wall) is gone. In its
 *              place a ray that leaves the map -- which only a damaged
 *              mirror could cause -- stops on a plain wall instead of
 *              reading outside the tilemap.
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

wallcast_t posts[MD_VIEW_MAX_W + 2];
long xintercept, yintercept;

static int xtilestep, ytilestep;
static int c_vx, c_vy;

#define NOTSAMETILE(x1, x2)                           \
  ((posts[(x1)].posttype != posts[(x2)].posttype) || \
   (posts[(x1)].offset != posts[(x2)].offset))

#define ONMAP(gx, gy) (((unsigned)(gx) | (unsigned)(gy)) < (unsigned)MAPSIZE)

static void Cast(int curx);
static void InitialCast(void);

static void Interpolate(int x1, int x2) {
  int i;
  int dtexture;
  int frac;
  int dheight;
  int hfrac;
  int dx;

  dx = x2 - x1;
  dtexture = (((posts[x2].texture - posts[x1].texture) << 12) + 0x800) / dx;
  dheight = (((posts[x2].wallheight - posts[x1].wallheight) << 8) + 0x80) / dx;
  frac = dtexture + (posts[x1].texture << 12);
  hfrac = dheight + (posts[x1].wallheight << 8);
  for (i = x1 + 1; i <= x2 - 1; i++, frac += dtexture, hfrac += dheight) {
    posts[i].lump = posts[x1].lump;
    posts[i].posttype = posts[x1].posttype;
    posts[i].offset = posts[x1].offset;
    posts[i].alttile = posts[x1].alttile;
    posts[i].texture = (frac >> 12);
    posts[i].wallheight = hfrac >> 8;
  }
}

void Refresh(void) {
  int x;

  if (viewwidth <= 0) return;

  InitialCast();

  for (x = 0; x <= viewwidth - 4; x += 4) {
    if NOTSAMETILE (x, x + 4) {
      Cast(x + 2);
      if NOTSAMETILE (x, x + 2) {
        Cast(x + 1);
      } else {
        Interpolate(x, x + 2);
      }
      if NOTSAMETILE (x + 2, x + 4) {
        Cast(x + 3);
      } else {
        Interpolate(x + 2, x + 4);
      }
    } else {
      Interpolate(x, x + 4);
    }
  }
}

static void HitWall(int curx, int vertical, int xtile, int ytile) {
  int num;

  posts[curx].offset = (xtile << 7) + ytile;
  posts[curx].lump = tilemap[xtile][ytile];
  posts[curx].alttile = 0;
  posts[curx].posttype = 0;

  if (vertical < 0) {
    xintercept = xtile << 16;
    if (xtilestep < 0) xintercept += 0xffff;
    yintercept = FixedScale(xintercept - viewx, c_vy, c_vx) + viewy;
    if (posts[curx].lump & 0x4000) {
      const int ax = xtile - (xtilestep >> 7);
      if (ONMAP(ax, ytile) && (tilemap[ax][ytile] & 0x8000)) {
        num = tilemap[ax][ytile];
        if (num & 0x4000) {
          if (maskobjlist[num & 0x3ff].sidepic)
            posts[curx].lump = maskobjlist[num & 0x3ff].sidepic;
          else
            posts[curx].lump &= 0x3ff;
        } else {
          posts[curx].lump = doorobjlist[num & 0x3ff].sidepic;
        }
      } else {
        if (posts[curx].lump & 0x1000)
          posts[curx].lump = animwalls[posts[curx].lump & 0x3ff];
        else
          posts[curx].lump &= 0x3ff;
      }
    } else if (posts[curx].lump & 0x2000) {
      if (IsWindow(xtile, ytile))
        posts[curx].alttile = -1;
      else
        posts[curx].alttile = R_Plane2(xtile, ytile) + 1;
      posts[curx].lump &= 0x3ff;
    } else if (posts[curx].lump & 0x1000) {
      posts[curx].lump = animwalls[posts[curx].lump & 0x3ff];
    } else if (posts[curx].lump & 0x800) {
      posts[curx].lump &= 0x3ff;
      posts[curx].posttype = 2;
    }
    posts[curx].texture = yintercept - (ytile << 16);
    if (posts[curx].texture < 0) posts[curx].texture = 0;
    if (posts[curx].texture > 65535) posts[curx].texture = 65535;
    if (xtilestep < 0) posts[curx].texture ^= 0xffff;
    posts[curx].posttype += 1;
  } else {
    yintercept = ytile << 16;
    if (ytilestep < 0) yintercept += 0xffff;
    xintercept = FixedScale(yintercept - viewy, c_vx, c_vy) + viewx;
    if (posts[curx].lump & 0x4000) {
      const int ay = ytile - ytilestep;
      if (ONMAP(xtile, ay) && (tilemap[xtile][ay] & 0x8000)) {
        num = tilemap[xtile][ay];
        if (num & 0x4000) {
          if (maskobjlist[num & 0x3ff].sidepic)
            posts[curx].lump = maskobjlist[num & 0x3ff].sidepic;
          else
            posts[curx].lump &= 0x3ff;
        } else {
          posts[curx].lump = doorobjlist[num & 0x3ff].sidepic;
        }
      } else {
        if (posts[curx].lump & 0x1000)
          posts[curx].lump = animwalls[posts[curx].lump & 0x3ff];
        else
          posts[curx].lump &= 0x3ff;
      }
    } else if (posts[curx].lump & 0x2000) {
      if (IsWindow(xtile, ytile))
        posts[curx].alttile = -1;
      else
        posts[curx].alttile = R_Plane2(xtile, ytile) + 1;
      posts[curx].lump &= 0x3ff;
    } else if (posts[curx].lump & 0x1000) {
      posts[curx].lump = animwalls[posts[curx].lump & 0x3ff];
    } else if (posts[curx].lump & 0x800) {
      posts[curx].lump &= 0x3ff;
      posts[curx].posttype = 2;
    }
    posts[curx].texture = xintercept - (xtile << 16);
    if (posts[curx].texture < 0) posts[curx].texture = 0;
    if (posts[curx].texture > 65535) posts[curx].texture = 65535;
    if (ytilestep > 0) posts[curx].texture ^= 0xffff;
  }
  posts[curx].wallheight = CalcHeight();
}

/* A ray that left the map: stop on the tile it was on with a plain wall,
 * as the Atari build's HitWallFar did for its step limit. */
static void HitEdge(int curx, int vertical, int xtile, int ytile) {
  word saved;

  if (xtile < 0) xtile = 0;
  if (xtile >= MAPSIZE) xtile = MAPSIZE - 1;
  if (ytile < 0) ytile = 0;
  if (ytile >= MAPSIZE) ytile = MAPSIZE - 1;
  saved = tilemap[xtile][ytile];
  tilemap[xtile][ytile] = (word)(wstart + 1);
  HitWall(curx, vertical, xtile, ytile);
  tilemap[xtile][ytile] = saved;
}

/* The body both casters share: march the ray from (grid) and hit. */
static void march(int curx, int cnt, const int *incr, const int *thedir) {
  int grid[2];
  int index;

  grid[0] = viewx >> 16;
  grid[1] = viewy >> 16;
  do {
    int tile;

    index = (cnt >= 0);
    cnt += incr[index];
    SET_SPOTVIS(grid[0], grid[1]);
    grid[index] += thedir[index];
    if (!ONMAP(grid[0], grid[1])) {
      HitEdge(curx, cnt - incr[index], grid[0], grid[1]);
      return;
    }

    if ((tile = tilemap[grid[0]][grid[1]]) != 0) {
      if (tile & 0x8000) {
        if ((!(tile & 0x4000)) &&
            (doorobjlist[tile & 0x3ff].action == dr_closed)) {
          SET_SPOTVIS(grid[0], grid[1]);
          if (doorobjlist[tile & 0x3ff].flags & DF_MULTI)
            MakeWideDoorVisible(tile & 0x3ff);
          do {
            index = (cnt >= 0);
            cnt += incr[index];
            grid[index] += thedir[index];
            if (!ONMAP(grid[0], grid[1])) {
              HitEdge(curx, cnt - incr[index], grid[0], grid[1]);
              return;
            }
            if ((tilemap[grid[0]][grid[1]] != 0) &&
                (!(tilemap[grid[0]][grid[1]] & 0x8000)))
              break;
          } while (1);
          break;
        } else {
          continue;
        }
      } else {
        R_MarkSeen(grid[0], grid[1]); /* mapseen[][]=1 */
        break;
      }
    }
  } while (1);
  HitWall(curx, cnt - incr[index], grid[0], grid[1]);
}

static int setup_ray(int *incr, int *thedir) {
  int snx = viewx & 0xffff;
  int sny = viewy & 0xffff;

  if (c_vx > 0) {
    thedir[0] = 1;
    xtilestep = 0x80;
    snx ^= 0xffff;
    incr[1] = -c_vx;
  } else {
    thedir[0] = -1;
    xtilestep = -0x80;
    incr[1] = c_vx;
  }
  if (c_vy > 0) {
    thedir[1] = 1;
    ytilestep = 1;
    sny ^= 0xffff;
    incr[0] = c_vy;
  } else {
    thedir[1] = -1;
    ytilestep = -1;
    incr[0] = -c_vy;
  }
  return FixedMul(snx, incr[0]) + FixedMul(sny, incr[1]);
}

static void InitialCast(void) {
  int incr[2];
  int thedir[2];
  int curx;

  c_vx = c_startx;
  c_vy = c_starty;
  for (curx = 0; curx <= viewwidth; curx += 4) {
    const int cnt = setup_ray(incr, thedir);
    march(curx, cnt, incr, thedir);
    c_vx += viewsin << 2;
    c_vy += viewcos << 2;
  }
}

static void Cast(int curx) {
  int incr[2];
  int thedir[2];

  c_vx = c_startx + (curx * viewsin);
  c_vy = c_starty + (curx * viewcos);
  const int cnt = setup_ray(incr, thedir);
  march(curx, cnt, incr, thedir);
}
