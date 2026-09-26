/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * File: r_local.h
 * Description: MD/ROTT renderer, private declarations.
 *
 * The renderer is ROTT's own (engine.c, rt_draw.c, rt_scale.c,
 * rt_floor.c from atarist-rott), ported to run on the Multi-device from a
 * render-only mirror of the ST's world. It keeps ROTT's names and
 * structure so the two can be compared line for line; what changed is
 * where the data comes from:
 *
 *   - lumps are read in place from the level pack (R_Lump), little-endian
 *     as in the WAD, so none of the ST's byte swapping applies;
 *   - game state (player, actors, statics) never reaches here: the ST
 *     sends a view block, an object list and the weapon;
 *   - the Atari survival cuts are undone: no ray step limit, real wall and
 *     sprite lighting, floors, ceilings and sky.
 *
 * Plain C with no Pico dependency, so tests/ can build it on the host.
 */

#ifndef R_LOCAL_H
#define R_LOCAL_H

#include <stdbool.h>
#include <stdint.h>

#include "rott_md_protocol.h"

typedef uint8_t byte;
typedef uint16_t word;
typedef int32_t fixed;

/* ------------------------------------------------------------------ */
/* Constants (rt_def.h, rt_view.h, _rt_draw.h, rt_door.h, rt_stat.h)    */
/* ------------------------------------------------------------------ */

#define MAPSIZE 128
#define ANGLES 2048
#define FINEANGLES 2048
#define FINEANGLEQUAD (FINEANGLES / 4)
#define ANG90 (FINEANGLES / 4)
#define ANG180 (ANG90 * 2)
#define HEIGHTFRACTION 6
#define DHEIGHTFRACTION 8
#define SFRACBITS 16
#define SFRACUNIT 0x10000
#define MINZ 0x2700
#define MAXVISIBLE 256
#define MAXVISIBLEDOORS 30
#define FIXEDTRANSLEVEL 30
#define MAXPLAYERCOLORS 11
#define PLAYERHEIGHT (260 << HEIGHTFRACTION)
#define MAXDOORS 150
#define MAXMASKED 300
#define MAXPWALLS 150
#define MAXANIMWALLS 17
#define MAXSKYSEGS 2048
#define MINSKYHEIGHT 0

#define DF_MULTI 0x04
#define MW_BOTTOMFLIPPING 0x800
#define MW_TOPFLIPPING 0x1000

/* doorobj_t action / pwallobj_t action values */
enum { dr_open, dr_closed, dr_opening, dr_closing };
enum { pw_npushed, pw_pushing, pw_pushed, pw_moving };
/* dirtype */
enum { east, northeast, north, northwest, west, southwest, south, southeast };

/* Render target: the chunky view buffer, one PLAYPAL index per pixel. */
#define R_PITCH MD_VIEW_MAX_W

/* ------------------------------------------------------------------ */
/* Lumps                                                                */
/* ------------------------------------------------------------------ */

/* The level pack (md_pack.c on the RP, a file on the host). NULL if the
 * pack does not hold the lump. */
const byte *R_Lump(int lump);      /* good for the current frame */
const byte *R_LumpFixed(int lump); /* preloaded only; good for the level */

/* patch_t / transpatch_t / font_t, as laid out in the WAD. Reading the
 * shorts in place is fine: lumps are 4-byte aligned in the pack and the
 * RP is little-endian like the WAD. */
typedef struct {
  int16_t origsize;
  int16_t width;
  int16_t height;
  int16_t leftoffset;
  int16_t topoffset;
  uint16_t collumnofs[1]; /* [width] */
} patch_t;

typedef struct {
  int16_t origsize;
  int16_t width;
  int16_t height;
  int16_t leftoffset;
  int16_t topoffset;
  int16_t translevel;
  int16_t collumnofs[1]; /* [width] */
} transpatch_t;

typedef struct {
  int16_t height;
  char width[256];
  int16_t charofs[256];
  byte data;
} font_t;

/* ------------------------------------------------------------------ */
/* World mirror (r_world.c)                                             */
/* ------------------------------------------------------------------ */

typedef struct {
  byte tilex, tiley;
  byte vertical;
  byte flags; /* DF_* */
  word texture;
  word alttexture;
  word sidepic;
  word basetexture;
  int32_t position;
  byte action;
} r_door_t;

typedef struct {
  byte tilex, tiley;
  byte vertical;
  byte active;
  word flags; /* MW_* */
  int16_t toptexture;
  int16_t midtexture;
  int16_t bottomtexture;
  word sidepic;
} r_mwall_t;

typedef struct {
  int32_t x, y;
  word texture;
  byte action;
} r_pwall_t;

extern word tilemap[MAPSIZE][MAPSIZE];
extern r_door_t doorobjlist[MAXDOORS];
extern int doornum;
extern r_mwall_t maskobjlist[MAXMASKED];
extern int maskednum;
extern r_pwall_t pwallobjlist[MAXPWALLS];
extern int pwallnum;
extern int animwalls[MAXANIMWALLS]; /* current texture of each */

/* Tiles the caster touched this frame (ROTT's spotvis[][] byte array, as
 * a bitset in the published layout to save 14 KB of RAM). */
extern uint16_t spotvis_bits[MD_BITSET_WORDS];
#define SPOTVIS(x, y) \
  ((spotvis_bits[MD_BITSET_WORD((x), (y))] >> MD_BITSET_BIT(y)) & 1u)
#define SET_SPOTVIS(x, y)                          \
  (spotvis_bits[MD_BITSET_WORD((x), (y))] |= \
   (uint16_t)(1u << MD_BITSET_BIT(y)))

void R_WorldReset(void);
/* Apply a record stream (WORLD or FRAME). Returns false on a malformed
 * stream. The FRAME-only records are handed to R_FrameRecord. */
bool R_ApplyRecords(const uint16_t *w, uint32_t nwords);
int R_Plane2(int x, int y); /* MAPSPOT(x, y, 2) for 0x2000 tiles */
uint32_t R_LightAt(int x, int y);
uint16_t R_TilemapCrc(void);
/* Mark a wall tile seen: in this frame's published bitset and in the
 * cumulative automap bitset. */
void R_MarkSeen(int x, int y);
extern uint16_t *r_mapseen_bits; /* cumulative; ROM4 on the RP */
extern uint16_t r_frame_bits[MD_BITSET_WORDS]; /* this frame's, see below */
void R_FinishFrameBits(void); /* fold spotvis into r_frame_bits */
void R_ClearFrameBits(void);

#define IsWindow(x, y) (R_Plane2((x), (y)) == 13)
#define M_ISDOOR(x, y) \
  ((tilemap[(x)][(y)] & 0x8000) && (!(tilemap[(x)][(y)] & 0x4000)))

/* ------------------------------------------------------------------ */
/* Level (r_main.c)                                                     */
/* ------------------------------------------------------------------ */

extern int maxheight, nominalheight, levelheight;
extern int wstart, shapestart, shapestop, gunsstart, elevatorstart;
extern int sky, centerskypost, skytoplump, skybottomlump;
extern int floorlump, ceilinglump, fontlump;
extern int difficulty;
extern byte *colormap; /* RAM copy with the fire colours fixed up */
extern const byte *redmap;
extern const byte *greenmap;
const byte *R_PlayerMap(int color);

/* ------------------------------------------------------------------ */
/* View (r_main.c)                                                      */
/* ------------------------------------------------------------------ */

extern fixed viewx, viewy;
extern int viewangle;
extern fixed viewsin, viewcos;
extern int c_startx, c_starty;
extern int pheight, nonbobpheight;
extern int viewwidth, viewheight;
extern int centerx, centery, centeryfrac;
extern fixed scale;
extern uint32_t heightnumerator;
extern int weaponscale;
extern int weaponbobx, weaponboby;
extern int16_t pixelangle[MD_VIEW_MAX_W];
extern int playerz;
extern int tictime;

extern int fulllight, fog, lightsource, lightning, lightninglevel;
extern int minshade, maxshade, normalshade, basemaxshade;
extern int gason, gasindex, transparentlevel, shrooms;

extern const int32_t *sintable; /* FINEANGLES + FINEANGLEQUAD + 1 */
extern const int32_t *costable;

extern byte *r_screen; /* top-left of the view in the chunky buffer */
extern int r_view_screen_x, r_view_screen_y; /* where the ST shows it */

/* ------------------------------------------------------------------ */
/* Fixed point (r_main.c)                                               */
/* ------------------------------------------------------------------ */

static inline fixed FixedMul(fixed a, fixed b) {
  return (fixed)(((int64_t)a * (int64_t)b + 0x8000) >> 16);
}
static inline fixed FixedMulShift(fixed a, fixed b, int shift) {
  return (fixed)(((uint64_t)((int64_t)a * (int64_t)b)) >> shift);
}
static inline fixed FixedScale(fixed orig, fixed factor, fixed divisor) {
  return (fixed)(((int64_t)orig * (int64_t)factor) / divisor);
}

/* ------------------------------------------------------------------ */
/* Ray casting (r_cast.c, engine.c)                                     */
/* ------------------------------------------------------------------ */

typedef struct {
  int offset;
  int wallheight;
  int ceilingclip;
  int floorclip;
  int texture;
  int lump;
  int posttype;
  int alttile;
} wallcast_t;

extern wallcast_t posts[MD_VIEW_MAX_W + 2];
extern long xintercept, yintercept;
void Refresh(void);

/* ------------------------------------------------------------------ */
/* Walls, doors, pushwalls (r_walls.c, rt_draw.c)                       */
/* ------------------------------------------------------------------ */

typedef struct {
  int viewheight;
  int viewx;
  int shapenum;
  int altshapenum;
  int shapesize;
  int x1, x2, h1, h2;
  int texturestart;
  int textureend;
  const byte *colormap;
} visobj_t;

extern visobj_t vislist[MAXVISIBLE], *visptr;

int CalcHeight(void);
bool TransformObject(int x, int y, int *dispx, int *dispheight);
bool TransformPlane(int x1, int y1, int x2, int y2, visobj_t *plane);
void SortVisibleList(int numvisible, visobj_t *vlist);
extern visobj_t *sortedvislist[MAXVISIBLE];
void TransformPushWalls(void);
void TransformDoors(void);
void DrawWalls(void);
void InterpolateDoor(visobj_t *plane);
void InterpolateMaskedWall(visobj_t *plane);
void MakeWideDoorVisible(int doornum);

/* ------------------------------------------------------------------ */
/* Floors, ceilings, sky (r_planes.c, rt_floor.c)                       */
/* ------------------------------------------------------------------ */

void DrawPlanes(void);
void DrawFlatBackdrop(void);

/* ------------------------------------------------------------------ */
/* Scaling (r_scale.c, rt_scale.c)                                      */
/* ------------------------------------------------------------------ */

extern const byte *shadingtable;
extern int dc_texturemid, dc_iscale, dc_invscale, sprtopoffset;
extern int dc_yl, dc_yh, centeryclipped;
extern const byte *dc_source;

void SetLightLevel(int height);
void SetPlayerLightLevel(void);
void ScaleShape(visobj_t *sprite);
void ScaleTransparentShape(visobj_t *sprite);
void ScaleSolidShape(visobj_t *sprite);
void ScaleWeapon(int xoff, int y, int shapenum);
void DrawScreenSprite(int x, int y, int shapenum);
void DrawScreenSizedSprite(int lump);
void ScaleMaskedPost(const byte *src, byte *buf);
void ScaleTransparentPost(const byte *src, byte *buf, int level);
void R_DrawWallColumn(byte *buf);

/* ------------------------------------------------------------------ */
/* Things (r_things.c)                                                  */
/* ------------------------------------------------------------------ */

/* The frame's object list, weapon and overlays, as the ST sent them. */
typedef struct {
  uint16_t x, y; /* 1/256 tile */
  int16_t z;
  uint16_t shapenum;
  uint16_t flags;
  uint16_t extra;
} r_obj_t;

#define R_MAX_OBJS 256
extern r_obj_t r_objs[R_MAX_OBJS];
extern int r_numobjs;

typedef struct {
  int16_t xoff, yoff;
  uint16_t shapenum;
  int16_t mode;
} r_weapon_t;
extern r_weapon_t r_weapon[2];
extern int r_numweapon;

typedef struct {
  uint16_t flags;
  uint16_t gmasklump;
  int16_t eyex, eyey;
  uint16_t eyeshape;
  uint16_t netlump;
} r_overlay_t;
extern r_overlay_t r_overlay;

void DrawScaleds(void);
void DrawPlayerWeapon(void);
void DrawOverlays(void);

/* ------------------------------------------------------------------ */
/* Text (r_text.c)                                                      */
/* ------------------------------------------------------------------ */

#define R_MAX_MSG_LINES 4
#define R_MAX_MSG_CHARS 64
extern char r_messages[R_MAX_MSG_LINES][R_MAX_MSG_CHARS];
extern int r_nummessages;
extern int r_bordercolor;
extern int r_paused;
void DrawMessages(void);
void DrawBorder(void);
void DrawPause(void);

/* ------------------------------------------------------------------ */
/* Frame (r_main.c)                                                     */
/* ------------------------------------------------------------------ */

/* Level parameters from LEVEL_BEGIN. False if a lump the renderer cannot
 * do without (the colormap) is missing. */
bool R_BeginLevel(const uint16_t *lv, unsigned nwords);

/* The view block of a FRAME. */
void R_SetView(const uint16_t *v);

/* Render the frame into `screen` (pitch R_PITCH), which must hold
 * viewheight rows. */
void R_RenderFrame(byte *screen);

/* Called for FRAME-only records by R_ApplyRecords. */
bool R_FrameRecord(unsigned type, unsigned count, const uint16_t *items);
void R_FrameReset(void);

#endif /* R_LOCAL_H */
