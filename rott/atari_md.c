/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
/*
 * atari_md.c - MD/ROTT: the ST side of the Multi-device renderer.
 * See atari_md.h for the overview and sidecart/include/rott_md_protocol.h
 * for every packet.
 *
 * Per level: LEVEL_BEGIN with the lumps the level needs (the MD builds its
 * level pack from the WAD on its SD card meanwhile), TILEMAP, WORLD with
 * doors, masked walls, pushwalls, animated walls, plane 2 and lights,
 * LEVEL_END, PALETTE.
 *
 * Per frame (I_FinishUpdate): world deltas, then a FRAME with the view,
 * weapon, overlays, messages and the objects that might be visible, then
 * the MD's newest frame copied to the screen. Pipelined by default: the MD
 * renders frame N while the ST runs the game for N+1, and the ST shows the
 * newest frame the MD has finished, at most one behind.
 */

#include "atari_md.h"

#if defined(__MINT__) && ATARI_MD_RENDER

#include "rt_def.h"

#include <mint/osbind.h>
#include <stdio.h>
#include <string.h>

#include "atari_c2p.h"
#include "i_timer.h"
#include "isr.h"
#include "lumpy.h"
#include "modexlib.h"
#include "rt_actor.h"
#include "rt_door.h"
#include "rt_draw.h"
#include "rt_floor.h"
#include "rt_main.h"
#include "rt_menu.h"
#include "rt_msg.h"
#include "rt_net.h"
#include "rt_playr.h"
#include "rt_stat.h"
#include "rt_str.h"
#include "rt_ted.h"
#include "rt_view.h"
#include "sidecart_md.h"
#include "sprites.h"
#include "states.h"
#include "w_wad.h"

#ifndef ATARI_MD_PIPELINE
#define ATARI_MD_PIPELINE 1
#endif
#ifndef ATARI_NOIR
#define ATARI_NOIR 0
#endif
#ifndef ATARI_NOIR_DITHERING
#define ATARI_NOIR_DITHERING 0
#endif

/* rt_draw.c / rt_view.c / rt_scale.c / rt_floor.c state the view needs */
extern int focalwidth;
extern int transparentlevel;
extern int lightninglevel;
extern boolean lightning;
extern int gunsstart;
extern int elevatorstart;
extern int fog;
extern int lightsource;
extern int maxheight;
extern int nominalheight;
extern int levelheight;
extern font_t *smallfont;
extern int G_gmasklump;
extern int ATARI_MD_SkyInfo(int *top, int *bottom, int *center, int *floorlump,
                            int *ceilinglump);
extern int StatRotate(statobj_t *temp);
extern int CalcRotate(objtype *ob);
#define FIXEDTRANSLEVEL (30) /* _rt_draw.h */

int atari_md_active;

#define MD_PKT_WORDS (MD_CMD_MAX_BYTES / 2)
#define MD_MAX_LUMPS 4096
#define MD_TILE_QUEUE 256
#define MD_LIGHT_QUEUE 128
#define MD_MAX_OBJS 128
#define MD_WAIT_MS 250
#define MD_PACK_WAIT_MS 90000

static unsigned short md_pkt[MD_PKT_WORDS];
static int md_pkt_len;
static int md_rec_count_at = -1;
static int md_rec_type;

static unsigned short md_lumpbits[MD_MAX_LUMPS / 16];
static int md_numlumps;
static int md_gfxlumps; /* lumps below the sounds (DIGISTRT) */

static unsigned long md_level_serial;
static int md_snapshot_needed;
static int md_snapshot_tries;
static unsigned short md_seq;
static unsigned short md_copied_seq;
static int md_frame_pending;
static int md_failures;

/* The frame being assembled */
static int md_view_x, md_view_y, md_view_w, md_view_h;
static int md_yzangle, md_nonbob, md_bobx, md_boby;
static unsigned short md_weapon[2][MD_WEAPON_WORDS];
static int md_numweapon;
static unsigned short md_overlay[MD_OVERLAY_WORDS];
static int md_border;
static int md_paused;

/* Deltas */
static unsigned short md_tileq[MD_TILE_QUEUE];
static int md_tileq_len;
static int md_tiles_overflow;
static unsigned short md_lightq[MD_LIGHT_QUEUE];
static int md_lightq_len;

typedef struct {
  word texture, alttexture;
  byte action, flags;
} md_door_shadow_t;
typedef struct {
  word flags;
  short top, mid, bottom;
} md_mwall_shadow_t;
typedef struct {
  long x, y;
  word texture;
  byte action;
} md_pwall_shadow_t;

static md_door_shadow_t md_doors[MAXDOORS];
static md_mwall_shadow_t md_mwalls[MAXMASKED];
static md_pwall_shadow_t md_pwalls[MAXPWALLS];
static int md_anims[MAXANIMWALLS];
static int md_doornum, md_maskednum, md_pwallnum;

/* Messages */
#define MD_MSG_LINES 4
#define MD_MSG_CHARS 64
static char md_msgs[MD_MSG_LINES][MD_MSG_CHARS];
static int md_nummsgs;
static int md_msgs_dirty;

/* Palette */
static unsigned char md_palette[768];
static int md_have_palette;
static unsigned long md_palette_serial;

/* ------------------------------------------------------------------ */
/* Packets                                                              */
/* ------------------------------------------------------------------ */

static void pkt_reset(void) {
  md_pkt_len = 0;
  md_rec_count_at = -1;
}

/* Room for `words` more item words (plus a record header and the END). */
static unsigned short *pkt_item(int type, int words) {
  if (md_rec_count_at < 0 || md_rec_type != type) {
    if (md_pkt_len + 2 + words + 1 > MD_PKT_WORDS) return NULL;
    md_pkt[md_pkt_len++] = (unsigned short)type;
    md_rec_count_at = md_pkt_len;
    md_pkt[md_pkt_len++] = 0;
    md_rec_type = type;
  } else if (md_pkt_len + words + 1 > MD_PKT_WORDS) {
    return NULL;
  }
  md_pkt[md_rec_count_at]++;
  md_pkt_len += words;
  return &md_pkt[md_pkt_len - words];
}

static void pkt_close(void) { md_rec_count_at = -1; }

static int world_flush(void) {
  int rc = 0;
  if (md_pkt_len) {
    md_pkt[md_pkt_len++] = MD_REC_END;
    rc = sidecart_md_write(MD_CMD_WORLD, md_pkt, md_pkt_len * 2,
                           (long)md_level_serial, 0L, 0L);
    pkt_reset();
  }
  return rc;
}

/* An item in a WORLD stream, flushing a full packet first. */
static unsigned short *world_item(int type, int words) {
  unsigned short *p = pkt_item(type, words);
  if (!p) {
    world_flush();
    p = pkt_item(type, words);
  }
  return p;
}

/* ------------------------------------------------------------------ */
/* Records                                                              */
/* ------------------------------------------------------------------ */

static void put_tile(unsigned short *p, int x, int y) {
  p[0] = (unsigned short)(x | (y << 8));
  p[1] = tilemap[x][y];
  p[2] = (tilemap[x][y] & 0x2000) ? (unsigned short)MAPSPOT(x, y, 2) : 0;
}

static void put_door(unsigned short *p, int i) {
  const doorobj_t *d = doorobjlist[i];
  p[0] = (unsigned short)i;
  p[1] = (unsigned short)(d->tilex | (d->tiley << 8));
  p[2] = (unsigned short)((d->vertical ? MD_DOOR_VERTICAL : 0) |
                          ((d->flags & 0xFF) << 8));
  p[3] = d->texture;
  p[4] = d->alttexture;
  p[5] = d->sidepic;
  p[6] = d->basetexture;
  p[7] = (unsigned short)d->position;
  p[8] = (unsigned short)d->action;
  md_doors[i].texture = d->texture;
  md_doors[i].alttexture = d->alttexture;
  md_doors[i].action = (byte)d->action;
  md_doors[i].flags = d->flags;
}

static void put_mwall(unsigned short *p, int i) {
  const maskedwallobj_t *m = maskobjlist[i];
  p[0] = (unsigned short)i;
  p[1] = (unsigned short)(m->tilex | (m->tiley << 8));
  p[2] = m->flags;
  p[3] = (unsigned short)((m->vertical ? MD_MW_VERTICAL : 0) |
                          ((m->flags & MW_ABP) ? MD_MW_ACTIVE : 0));
  p[4] = (unsigned short)m->toptexture;
  p[5] = (unsigned short)m->midtexture;
  p[6] = (unsigned short)m->bottomtexture;
  p[7] = (unsigned short)m->sidepic;
  md_mwalls[i].flags = m->flags;
  md_mwalls[i].top = m->toptexture;
  md_mwalls[i].mid = m->midtexture;
  md_mwalls[i].bottom = m->bottomtexture;
}

static void put_pwall(unsigned short *p, int i) {
  const pwallobj_t *w = pwallobjlist[i];
  p[0] = (unsigned short)i;
  md_put32(p + 1, (uint32_t)w->x);
  md_put32(p + 3, (uint32_t)w->y);
  p[5] = w->texture;
  p[6] = (unsigned short)w->action;
  md_pwalls[i].x = w->x;
  md_pwalls[i].y = w->y;
  md_pwalls[i].texture = w->texture;
  md_pwalls[i].action = (byte)w->action;
}

static void put_anim(unsigned short *p, int i) {
  p[0] = (unsigned short)i;
  p[1] = (unsigned short)animwalls[i].texture;
  md_anims[i] = animwalls[i].texture;
}

static void put_light(unsigned short *p, int x, int y) {
  p[0] = (unsigned short)(x | (y << 8));
  md_put32(p + 1, (uint32_t)LightSourceAt(x, y));
}

static void put_counts(unsigned short *p) {
  p[0] = (unsigned short)doornum;
  p[1] = (unsigned short)maskednum;
  p[2] = (unsigned short)pwallnum;
  md_doornum = doornum;
  md_maskednum = maskednum;
  md_pwallnum = pwallnum;
}

/* ------------------------------------------------------------------ */
/* Startup                                                              */
/* ------------------------------------------------------------------ */

static int read_wad_header(const char *name, long *numlumps, long *dirofs,
                           long *size) {
  unsigned char hdr[12];
  long fh = Fopen(name, 0);
  if (fh < 0) return 0;
  if (Fread((short)fh, 12, hdr) != 12) {
    Fclose((short)fh);
    return 0;
  }
  *size = Fseek(0L, (short)fh, 2);
  Fclose((short)fh);
  *numlumps = (long)hdr[4] | ((long)hdr[5] << 8) | ((long)hdr[6] << 16) |
              ((long)hdr[7] << 24);
  *dirofs = (long)hdr[8] | ((long)hdr[9] << 8) | ((long)hdr[10] << 16) |
            ((long)hdr[11] << 24);
  return 1;
}

void ATARI_MD_Init(void) {
#if (SHAREWARE)
  static const char wadname[] = "HUNTBGIN.WAD";
#else
  static const char wadname[] = "DARKWAR.WAD";
#endif
  unsigned short hello[MD_HELLO_WORDS];
  char result[80];
  long numlumps, dirofs, size;
  long flags = 0;
  int rc;

  atari_md_active = 0;
  if (!sidecart_md_present()) return;

  if (!read_wad_header(wadname, &numlumps, &dirofs, &size)) {
    printf("ROTT Accelerator: cannot read %s\n", wadname);
    return;
  }
  md_numlumps = (int)numlumps;
  if (md_numlumps > MD_MAX_LUMPS) md_numlumps = MD_MAX_LUMPS;
  md_gfxlumps = W_CheckNumForName((char *)"DIGISTRT");
  if (md_gfxlumps <= 0 || md_gfxlumps > md_numlumps) md_gfxlumps = md_numlumps;

  memset(hello, 0, sizeof(hello));
  hello[MD_HELLO_NUMLUMPS] = (unsigned short)numlumps;
  md_put32(hello + MD_HELLO_WADSIZE, (uint32_t)size);
  md_put32(hello + MD_HELLO_DIROFS, (uint32_t)dirofs);
  /* The name as ST-order bytes, as md_get_bytes() expects. */
  strncpy((char *)(hello + MD_HELLO_NAME), wadname, MD_HELLO_NAME_WORDS * 2 - 1);
  if (ATARI_NOIR) flags |= MD_HELLO_NOIR;

  sidecart_md_bus_begin();
  md_command_timeout = MD_TIMEOUT_DETECT;
  rc = sidecart_md_write(MD_CMD_HELLO, hello, sizeof(hello),
                         (long)MD_HELLO_MAGIC, (long)MD_PROTOCOL_VERSION,
                         flags);
  md_command_timeout = MD_TIMEOUT_NORMAL;
  if (rc == 0) {
    /* The firmware answers from its main loop, a moment after the ack. */
    long t0 = I_GetTimeMS();
    while (MD_STATUS[MD_ST_LAST_CMD] != MD_CMD_HELLO &&
           I_GetTimeMS() - t0 < 1000) {
    }
  }
  sidecart_md_result(result, sizeof(result));
  sidecart_md_bus_end();

  if (rc != 0) {
    printf("ROTT Accelerator not answering\nUsing the ST renderer\n");
    return;
  }
  if (MD_STATUS[MD_ST_MAGIC] != MD_STATUS_MAGIC ||
      MD_STATUS[MD_ST_PROTO] != MD_PROTOCOL_VERSION) {
    printf("ROTT Accelerator protocol %u, need %u\nUsing the ST renderer\n",
           (unsigned)MD_STATUS[MD_ST_PROTO], (unsigned)MD_PROTOCOL_VERSION);
    return;
  }
  if (MD_STATUS[MD_ST_ERRORS] &
      (MD_ERR_NO_SD | MD_ERR_NO_WAD | MD_ERR_WAD_MISMATCH)) {
    printf("%s\nUsing the ST renderer\n", result);
    return;
  }
  printf("%s\n", result);
  atari_md_active = 1;
  md_snapshot_needed = 1;
}

/* ------------------------------------------------------------------ */
/* Level                                                                */
/* ------------------------------------------------------------------ */

void ATARI_MD_BeginLumpList(void) {
  memset(md_lumpbits, 0, sizeof(md_lumpbits));
}

void ATARI_MD_AddLump(int lump) {
  /* Sounds stay on the ST (the precache list carries them too). */
  if (lump > 0 && lump < md_gfxlumps && W_LumpLength(lump) > 0) {
    md_lumpbits[lump >> 4] |= (unsigned short)(1u << (lump & 15));
  }
}

static void add_lump_range(int first, int last) {
  int i;
  for (i = first; i <= last; i++) ATARI_MD_AddLump(i);
}

static void add_lump_name(const char *name) {
  int lump = W_CheckNumForName((char *)name);
  if (lump >= 0) ATARI_MD_AddLump(lump);
}

static void add_lump_names(const char *first, const char *last) {
  int a = W_CheckNumForName((char *)first);
  int b = W_CheckNumForName((char *)last);
  if (a >= 0 && b >= a) add_lump_range(a, b);
}

void ATARI_MD_LevelChanged(void) {
  if (atari_md_active) md_snapshot_needed = 1;
  md_tileq_len = 0;
  md_tiles_overflow = 0;
  md_lightq_len = 0;
}

void ATARI_MD_TileChanged(int x, int y) {
  if (md_tileq_len < MD_TILE_QUEUE) {
    md_tileq[md_tileq_len++] = (unsigned short)(x | (y << 8));
  } else {
    md_tiles_overflow = 1;
  }
}

void ATARI_MD_LightsChanged(int x, int y) {
  int dx, dy;
  if (!atari_md_active || !lightsource) return;
  for (dx = -1; dx <= 1; dx++) {
    for (dy = -1; dy <= 1; dy++) {
      int tx = x + dx, ty = y + dy;
      if (tx < 0 || ty < 0 || tx >= MAPSIZE || ty >= MAPSIZE) continue;
      if (md_lightq_len < MD_LIGHT_QUEUE) {
        md_lightq[md_lightq_len++] = (unsigned short)(tx | (ty << 8));
      } else {
        md_snapshot_needed = 1;
      }
    }
  }
}

static void send_palette(void) {
  long flags = 0;
  if (!md_have_palette) return;
  if (ATARI_NOIR) flags |= MD_PAL_NOIR;
  if (ATARI_NOIR_DITHERING) flags |= MD_PAL_NOIR_DITHER;
  sidecart_md_write(MD_CMD_PALETTE, md_palette, 768, (long)++md_palette_serial,
                    flags, 0L);
}

void ATARI_MD_SetPalette(const unsigned char *palette) {
  if (md_have_palette && !memcmp(md_palette, palette, 768)) return;
  memcpy(md_palette, palette, 768);
  md_have_palette = 1;
  if (atari_md_active && !md_snapshot_needed) send_palette();
}

/* Draw a line of progress into the view while the MD builds the pack, with
 * the ST's own renderer's path (the view is black at this point). */
static void show_progress(int percent) {
  extern void I_FinishUpdate(void);
  extern byte *bufferofs;
  extern int screenofs;
  char text[48];
  int y, w, h;
  byte *base = bufferofs + screenofs;

  for (y = 0; y < viewheight; y++) memset(base + ylookup[y], 0, viewwidth);
  sprintf(text, "Preparing level %d%%", percent);
  CurrentFont = smallfont;
  VW_MeasurePropString(text, &w, &h);
  px = (viewwidth - w) / 2 + (screenofs % iGLOBAL_SCREENWIDTH);
  py = (screenofs / iGLOBAL_SCREENWIDTH) + (viewheight / 2) - 4;
  VW_DrawPropString(text);
  atari_md_active = 0; /* the plain C2P path, just this once */
  I_FinishUpdate();
  atari_md_active = 1;
}

static int wait_level_state(void) {
  long t0 = I_GetTimeMS();
  long last = -1000;
  int state;
  for (;;) {
    state = MD_STATUS[MD_ST_LEVEL_STATE];
    if (MD_STATUS[MD_ST_LEVEL_SEQ] == (unsigned short)md_level_serial &&
        state != MD_LEVEL_LOADING) {
      return state;
    }
    if (I_GetTimeMS() - last > 250) {
      last = I_GetTimeMS();
      I_GetTime(); /* keeps the music going */
      show_progress(MD_STATUS[MD_ST_PROGRESS]);
    }
    if (I_GetTimeMS() - t0 > MD_PACK_WAIT_MS) return MD_LEVEL_ERROR;
  }
}

static int send_snapshot(void) {
  static unsigned short lv[MD_LV_WORDS];
  int skytop, skybottom, skycenter, floorlump, ceilinglump;
  int i, x, y, state;
  const int bitwords = (md_numlumps + 15) >> 4;

  md_level_serial++;
  md_snapshot_needed = 0;

  /* What the renderer reads besides ROTT's precache list, which is built
   * for the DOS memory manager: colour maps (uniform colours only with
   * other players), planes, sky, font, the weapons (precached only in
   * development builds), overlays. The MD loads anything else it turns
   * out to need from its own copy of the WAD. */
  i = ATARI_MD_SkyInfo(&skytop, &skybottom, &skycenter, &floorlump,
                       &ceilinglump);
  add_lump_name("colormap");
  x = W_CheckNumForName("specmaps");
  if (x >= 0) add_lump_range(x + 1, x + 1);
  x = W_CheckNumForName("playmaps");
  if (x >= 0 && numplayers > 1) add_lump_range(x + 1, x + MAXPLAYERCOLORS);
  ATARI_MD_AddLump(floorlump);
  ATARI_MD_AddLump(ceilinglump);
  if (i) {
    ATARI_MD_AddLump(skytop);
    ATARI_MD_AddLump(skybottom);
  }
  add_lump_name("smallfont");
  ATARI_MD_AddLump(G_gmasklump);
#if (SHAREWARE == 0)
  add_lump_names("KNIFE1", "DOGPAW4");
#else
  add_lump_names("MPIST11", "GODHAND8");
#endif
  add_lump_range(shapestart + GIBEYE1, shapestart + GIBEYE3);

  memset(lv, 0, sizeof(lv));
  lv[MD_LV_MAPON] = (unsigned short)gamestate.mapon;
  lv[MD_LV_SKY] = (unsigned short)(i ? sky : 0);
  lv[MD_LV_SKYTOP] = (unsigned short)skytop;
  lv[MD_LV_SKYBOTTOM] = (unsigned short)skybottom;
  lv[MD_LV_SKYCENTER] = (unsigned short)skycenter;
  lv[MD_LV_FLOOR] = (unsigned short)floorlump;
  lv[MD_LV_CEILING] = (unsigned short)ceilinglump;
  lv[MD_LV_FLAGS] = (unsigned short)((lightning ? MD_LVF_LIGHTNING : 0) |
                                     (fog ? MD_LVF_FOG : 0) |
                                     (lightsource ? MD_LVF_LIGHTSOURCE : 0));
  lv[MD_LV_MAXHEIGHT] = (unsigned short)maxheight;
  lv[MD_LV_NOMINALHEIGHT] = (unsigned short)nominalheight;
  lv[MD_LV_LEVELHEIGHT] = (unsigned short)levelheight;
  lv[MD_LV_COLORMAP] = (unsigned short)W_CheckNumForName("colormap");
  lv[MD_LV_SPECMAPS] = (unsigned short)W_CheckNumForName("specmaps");
  lv[MD_LV_PLAYMAPS] = (unsigned short)W_CheckNumForName("playmaps");
  lv[MD_LV_WSTART] = (unsigned short)wstart;
  lv[MD_LV_SHAPESTART] = (unsigned short)shapestart;
  lv[MD_LV_SHAPESTOP] = (unsigned short)shapestop;
  lv[MD_LV_GUNSSTART] = (unsigned short)gunsstart;
  lv[MD_LV_ELEVSTART] = (unsigned short)elevatorstart;
  lv[MD_LV_FONT] = (unsigned short)W_CheckNumForName("smallfont");
  lv[MD_LV_DIFFICULTY] = (unsigned short)gamestate.difficulty;

  /* LEVEL_BEGIN: the parameters and the lump bitset in one buffer. */
  pkt_reset();
  memcpy(md_pkt, lv, sizeof(lv));
  memcpy(md_pkt + MD_LV_WORDS, md_lumpbits, bitwords * 2);
  if (sidecart_md_write(MD_CMD_LEVEL_BEGIN, md_pkt,
                        (MD_LV_WORDS + bitwords) * 2, (long)md_level_serial,
                        (long)md_numlumps, 0L)) {
    return 0;
  }
  state = wait_level_state();
  if (state != MD_LEVEL_PACKED) return 0;

  /* TILEMAP, 1024 tiles a command. */
  for (i = 0; i < MAPSIZE * MAPSIZE; i += MD_PKT_WORDS) {
    if (sidecart_md_write(MD_CMD_TILEMAP, &tilemap[0][0] + i,
                          MD_PKT_WORDS * 2, (long)i, (long)MD_PKT_WORDS, 0L)) {
      return 0;
    }
  }

  /* WORLD */
  pkt_reset();
  put_counts(world_item(MD_REC_COUNTS, MD_COUNTS_WORDS));
  for (i = 0; i < doornum; i++) put_door(world_item(MD_REC_DOOR, MD_DOOR_WORDS), i);
  for (i = 0; i < maskednum; i++)
    put_mwall(world_item(MD_REC_MWALL, MD_MWALL_WORDS), i);
  for (i = 0; i < pwallnum; i++)
    put_pwall(world_item(MD_REC_PWALL, MD_PWALL_WORDS), i);
  for (i = 0; i < MAXANIMWALLS; i++)
    put_anim(world_item(MD_REC_ANIM, MD_ANIM_WORDS), i);
  for (x = 0; x < MAPSIZE; x++) {
    for (y = 0; y < MAPSIZE; y++) {
      if (tilemap[x][y] & 0x2000)
        put_tile(world_item(MD_REC_TILE, MD_TILE_WORDS), x, y);
    }
  }
  if (lightsource && lights) {
    for (x = 0; x < MAPSIZE; x++) {
      for (y = 0; y < MAPSIZE; y++) {
        if (LightSourceAt(x, y))
          put_light(world_item(MD_REC_LIGHT, MD_LIGHT_WORDS), x, y);
      }
    }
  }
  if (world_flush()) return 0;
  if (sidecart_md_command(MD_CMD_LEVEL_END, (long)md_level_serial, 0L)) return 0;
  send_palette();

  md_tileq_len = 0;
  md_tiles_overflow = 0;
  md_lightq_len = 0;
  md_copied_seq = MD_STATUS[MD_ST_READY_SEQ];
  md_msgs_dirty = 1;
  return 1;
}

/* ------------------------------------------------------------------ */
/* Frame                                                                */
/* ------------------------------------------------------------------ */

/* Where the view goes on screen: the ST's own C2P zooms views up to 160
 * wide to fill the space between the bars (I_FinishUpdate,
 * atari_c2p_screen with ATARI_C2P_STRICT_NO_OVERLAP); the MD renders that
 * destination rect natively. */
static void view_rect(void) {
  extern int screenofs;
  const int sy = screenofs / iGLOBAL_SCREENWIDTH;
  const int sx = screenofs - sy * iGLOBAL_SCREENWIDTH;
  int zoom = 1;

  md_view_x = sx;
  md_view_y = sy;
  md_view_w = viewwidth;
  md_view_h = viewheight;
  if (viewwidth <= 80)
    zoom = 4;
  else if (viewwidth <= 160)
    zoom = 2;
  if (zoom > 1) {
    const int top = SHOW_TOP_STATUS_BAR() ? 16 : 0;
    const int bottom = 200 - (SHOW_BOTTOM_STATUS_BAR() ? 16 : 0);
    const int cx = sx + (viewwidth >> 1);
    const int cy = sy + (viewheight >> 1);
    int src_h = viewheight;
    if (src_h * zoom > bottom - top) src_h = (bottom - top) / zoom;
    md_view_w = viewwidth * zoom;
    md_view_h = src_h * zoom;
    md_view_x = cx - (md_view_w >> 1);
    md_view_y = cy - (md_view_h >> 1);
    if (md_view_y < top) md_view_y = top;
    if (md_view_y + md_view_h > bottom) md_view_y = bottom - md_view_h;
  }
  if (md_view_w > MD_VIEW_MAX_W) md_view_w = MD_VIEW_MAX_W;
  if (md_view_h > MD_VIEW_MAX_H) md_view_h = MD_VIEW_MAX_H;
  md_view_x &= ~15;
  md_view_w &= ~31;
}

void ATARI_MD_BeginFrame(int yzangle, int nonbobpheight, int weaponbobx,
                         int weaponboby) {
  md_yzangle = yzangle;
  md_nonbob = nonbobpheight;
  md_bobx = weaponbobx;
  md_boby = weaponboby;
  md_numweapon = 0;
  memset(md_overlay, 0, sizeof(md_overlay));
  md_paused = 0;
  view_rect();
  md_frame_pending = 1;
}

void ATARI_MD_Weapon(int xoff, int yoff, int shapenum, int mode) {
  if (md_numweapon < 2) {
    unsigned short *w = md_weapon[md_numweapon++];
    w[0] = (unsigned short)xoff;
    w[1] = (unsigned short)yoff;
    w[2] = (unsigned short)shapenum;
    w[3] = (unsigned short)mode;
  }
}

void ATARI_MD_Eye(int x, int y, int shapenum) {
  md_overlay[0] |= MD_OV_EYE;
  md_overlay[2] = (unsigned short)x;
  md_overlay[3] = (unsigned short)y;
  md_overlay[4] = (unsigned short)shapenum;
}

void ATARI_MD_GasMask(int lump) {
  md_overlay[0] |= MD_OV_GASMASK;
  md_overlay[1] = (unsigned short)lump;
}

void ATARI_MD_Border(int color) { md_border = color; }

void ATARI_MD_Paused(void) { md_paused = 1; }

void ATARI_MD_Messages(const char *const *lines, int count) {
  int i;
  if (count > MD_MSG_LINES) count = MD_MSG_LINES;
  if (count != md_nummsgs) md_msgs_dirty = 1;
  for (i = 0; i < count; i++) {
    if (strncmp(md_msgs[i], lines[i], MD_MSG_CHARS - 1)) {
      strncpy(md_msgs[i], lines[i], MD_MSG_CHARS - 1);
      md_msgs[i][MD_MSG_CHARS - 1] = 0;
      md_msgs_dirty = 1;
    }
  }
  md_nummsgs = count;
}

/* The MD's spotvis of its newest frame: is the tile at (x, y) or one of
 * its eight neighbours marked? */
static int spotvis_near(int x, int y) {
  volatile const unsigned short *bits =
      (volatile const unsigned short *)(MD_ROM4_BASE + MD_SPOTVIS_OFFSET);
  int dx, dy;
  if (x < 1 || y < 1 || x >= MAPSIZE - 1 || y >= MAPSIZE - 1) return 0;
  for (dx = -1; dx <= 1; dx++) {
    const int base = (x + dx) << 7;
    for (dy = -1; dy <= 1; dy++) {
      const int t = base + y + dy;
      if ((bits[t >> 4] >> (t & 15)) & 1) return 1;
    }
  }
  return 0;
}

static int near_player(int tx, int ty) {
  int dx = tx - player->tilex;
  int dy = ty - player->tiley;
  return dx >= -2 && dx <= 2 && dy >= -2 && dy <= 2;
}

static unsigned short *obj_item(void) { return pkt_item(MD_REC_OBJS, MD_OBJ_WORDS); }

static void put_obj(unsigned short *p, fixed x, fixed y, int z, int shapenum,
                    int flags, int extra) {
  p[0] = (unsigned short)((unsigned long)x >> 8);
  p[1] = (unsigned short)((unsigned long)y >> 8);
  p[2] = (unsigned short)z;
  p[3] = (unsigned short)shapenum;
  p[4] = (unsigned short)flags;
  p[5] = (unsigned short)extra;
}

#define HF_1 (24)
#define HF_2 (72)

static int disk_shape(int value) {
  if ((value <= HF_2) && (value > HF_1)) return 1;
  if ((value <= HF_1) && (value >= -HF_1)) return 2;
  if ((value < -HF_1) && (value >= -HF_2)) return 3;
  if (value < -HF_2) return 4;
  return 0;
}

/* DrawScaleds' object half: which statics and actors to send and how to
 * light them. Rotation and the height flips are resolved here because they
 * need game state; the MD does the rest. */
static void add_objects(void) {
  statobj_t *statptr;
  objtype *obj;
  int count = 0;

  for (statptr = firstactivestat; statptr && count < MD_MAX_OBJS;
       statptr = statptr->nextactive) {
    int shapenum, flags, extra = 0;
    unsigned short *p;

    if (statptr->shapenum == NOTHING) continue;
    if (!spotvis_near(statptr->tilex, statptr->tiley) &&
        !near_player(statptr->tilex, statptr->tiley)) {
      statptr->flags &= ~FL_VISIBLE;
      continue;
    }
    statptr->flags |= FL_SEEN | FL_VISIBLE;

    shapenum = statptr->shapenum + shapestart;
    if (statptr->flags & FL_ROTATING) shapenum += StatRotate(statptr);

    if (statptr->flags & FL_TRANSLUCENT) {
      flags = MD_OF_TRANSLUCENT;
      extra = (statptr->flags & FL_FADING) ? transparentlevel : FIXEDTRANSLEVEL;
    } else if (statptr->flags & FL_SOLIDCOLOR) {
      flags = MD_OF_SOLID;
      extra = statptr->hitpoints;
    } else if (statptr->flags & FL_COLORED) {
      flags = MD_OF_COLORED;
      extra = statptr->hitpoints;
    } else {
      flags = MD_OF_NORMAL;
    }
    if (statptr->flags & FL_FULLLIGHT) flags |= MD_OF_FULLBRIGHT;
    flags |= MD_OF_EASTWEST; /* statics light as dir 0 = east */

    if ((statptr->itemnumber != (unsigned int)-1) &&
        (statptr->flags & FL_HEIGHTFLIPPABLE)) {
      if (statptr->itemnumber == stat_disk)
        shapenum += disk_shape(md_nonbob - statptr->z - 32);
      else if ((md_nonbob - statptr->z) < -16)
        shapenum++;
    }

    p = obj_item();
    if (!p) return;
    put_obj(p, statptr->x, statptr->y, statptr->z, shapenum, flags, extra);
    count++;
  }

  for (obj = firstactive; obj && count < MD_MAX_OBJS; obj = obj->nextactive) {
    int shapenum, flags = MD_OF_NORMAL, extra = 0;
    unsigned short *p;

    if (obj == player) continue;
    if (obj->shapenum == NOTHING) continue;
    if (!spotvis_near(obj->tilex, obj->tiley) &&
        !near_player(obj->tilex, obj->tiley)) {
      obj->flags &= ~FL_VISIBLE;
      continue;
    }
    obj->flags |= FL_SEEN | FL_VISIBLE;

    shapenum = obj->shapenum + shapestart;
    if (obj->state->rotate) shapenum += CalcRotate(obj);

    if (player->flags & FL_SHROOMS) {
      flags = MD_OF_SOLID;
      extra = GetTicCount() & 0xff;
    }
    if (obj->obclass == playerobj) {
      if (obj->flags & FL_GODMODE) {
        flags = MD_OF_SOLID;
        extra = 240 + (GetTicCount() & 0x7);
      } else if (obj->flags & FL_COLORED) {
        playertype *pstate;
        M_LINKSTATE(obj, pstate);
        flags = MD_OF_COLORED;
        extra = pstate->uniformcolor;
      }
    } else if ((obj->obclass >= b_darianobj) &&
               (obj->obclass <= b_robobossobj) && MISCVARS->redindex) {
      flags = MD_OF_REDMAP;
      extra = MISCVARS->redindex;
    }
    if (obj->flags & FL_FULLLIGHT) flags |= MD_OF_FULLBRIGHT;
    if (obj->dir == east || obj->dir == west) flags |= MD_OF_EASTWEST;

    if (obj->obclass == diskobj)
      shapenum += disk_shape(md_nonbob - obj->z - 32);
    else if ((obj->obclass == pillarobj) && ((md_nonbob - obj->z) < -16))
      shapenum++;

    p = obj_item();
    if (!p) return;
    put_obj(p, obj->x, obj->y, obj->z, shapenum, flags, extra);
    count++;
  }
}

/* World deltas since the last frame, into the packet (flushed as WORLD
 * commands when it fills). */
static void add_deltas(void) {
  int i;

  if (md_tiles_overflow) {
    /* Too many tile writes to track: resend the whole tilemap. */
    world_flush();
    for (i = 0; i < MAPSIZE * MAPSIZE; i += MD_PKT_WORDS) {
      sidecart_md_write(MD_CMD_TILEMAP, &tilemap[0][0] + i, MD_PKT_WORDS * 2,
                        (long)i, (long)MD_PKT_WORDS, 0L);
    }
    md_tiles_overflow = 0;
  }
  for (i = 0; i < md_tileq_len; i++) {
    const int x = md_tileq[i] & 0xFF;
    const int y = md_tileq[i] >> 8;
    put_tile(world_item(MD_REC_TILE, MD_TILE_WORDS), x, y);
  }
  md_tileq_len = 0;

  if (doornum != md_doornum || maskednum != md_maskednum ||
      pwallnum != md_pwallnum) {
    const int old_doors = md_doornum, old_masked = md_maskednum,
              old_pwalls = md_pwallnum;
    put_counts(world_item(MD_REC_COUNTS, MD_COUNTS_WORDS));
    for (i = old_doors; i < doornum; i++)
      put_door(world_item(MD_REC_DOOR, MD_DOOR_WORDS), i);
    for (i = old_masked; i < maskednum; i++)
      put_mwall(world_item(MD_REC_MWALL, MD_MWALL_WORDS), i);
    for (i = old_pwalls; i < pwallnum; i++)
      put_pwall(world_item(MD_REC_PWALL, MD_PWALL_WORDS), i);
  }
  for (i = 0; i < doornum; i++) {
    const doorobj_t *d = doorobjlist[i];
    const md_door_shadow_t *s = &md_doors[i];
    if (d->texture != s->texture || d->alttexture != s->alttexture ||
        (byte)d->action != s->action || d->flags != s->flags)
      put_door(world_item(MD_REC_DOOR, MD_DOOR_WORDS), i);
  }
  for (i = 0; i < maskednum; i++) {
    const maskedwallobj_t *m = maskobjlist[i];
    const md_mwall_shadow_t *s = &md_mwalls[i];
    if (m->flags != s->flags || m->toptexture != s->top ||
        m->midtexture != s->mid || m->bottomtexture != s->bottom)
      put_mwall(world_item(MD_REC_MWALL, MD_MWALL_WORDS), i);
  }
  for (i = 0; i < pwallnum; i++) {
    const pwallobj_t *w = pwallobjlist[i];
    const md_pwall_shadow_t *s = &md_pwalls[i];
    if (w->x != s->x || w->y != s->y || w->texture != s->texture ||
        (byte)w->action != s->action)
      put_pwall(world_item(MD_REC_PWALL, MD_PWALL_WORDS), i);
  }
  for (i = 0; i < MAXANIMWALLS; i++) {
    if (animwalls[i].texture != md_anims[i])
      put_anim(world_item(MD_REC_ANIM, MD_ANIM_WORDS), i);
  }
  if (lights) {
    for (i = 0; i < md_lightq_len; i++) {
      put_light(world_item(MD_REC_LIGHT, MD_LIGHT_WORDS), md_lightq[i] & 0xFF,
                md_lightq[i] >> 8);
    }
  }
  md_lightq_len = 0;
  pkt_close();
}

static void add_view(void) {
  unsigned short *v = pkt_item(MD_REC_VIEW, MD_VIEW_WORDS);
  unsigned short flags = 0;
  if (fulllight) flags |= MD_VF_FULLLIGHT;
  if (fog) flags |= MD_VF_FOG;
  if (lightning) flags |= MD_VF_LIGHTNING;
  if (MISCVARS->GASON == 1) flags |= MD_VF_GASON;
  if (player->flags & FL_SHROOMS) flags |= MD_VF_SHROOMS;
  if (md_paused) flags |= MD_VF_PAUSED;
  if (lightsource && lights) flags |= MD_VF_LIGHTSOURCE;

  md_put32(v + MD_V_VIEWX, (uint32_t)viewx);
  md_put32(v + MD_V_VIEWY, (uint32_t)viewy);
  v[MD_V_ANGLE] = (unsigned short)viewangle;
  v[MD_V_PHEIGHT] = (unsigned short)pheight;
  v[MD_V_NONBOB] = (unsigned short)md_nonbob;
  v[MD_V_YZANGLE] = (unsigned short)md_yzangle;
  v[MD_V_FOCAL] = (unsigned short)focalwidth;
  v[MD_V_BOBX] = (unsigned short)md_bobx;
  v[MD_V_BOBY] = (unsigned short)md_boby;
  v[MD_V_SCREENX] = (unsigned short)md_view_x;
  v[MD_V_SCREENY] = (unsigned short)md_view_y;
  v[MD_V_WIDTH] = (unsigned short)md_view_w;
  v[MD_V_HEIGHT] = (unsigned short)md_view_h;
  v[MD_V_FLAGS] = flags;
  v[MD_V_MINSHADE] = (unsigned short)minshade;
  v[MD_V_MAXSHADE] = (unsigned short)maxshade;
  v[MD_V_NORMALSHADE] = (unsigned short)normalshade;
  v[MD_V_BASEMAXSHADE] = (unsigned short)basemaxshade;
  v[MD_V_LIGHTNINGLVL] = (unsigned short)lightninglevel;
  v[MD_V_GASINDEX] = (unsigned short)MISCVARS->gasindex;
  v[MD_V_TRANSLEVEL] = (unsigned short)transparentlevel;
  v[MD_V_TICS] = (unsigned short)GetTicCount();
  v[MD_V_PLAYERZ] = (unsigned short)player->z;
  v[MD_V_BORDER] = (unsigned short)md_border;
  pkt_close();
}

static void add_messages(void) {
  int i, words = 0;
  unsigned short *p;
  if (!md_msgs_dirty) return;
  for (i = 0; i < md_nummsgs; i++)
    words += 1 + ((int)strlen(md_msgs[i]) + 1) / 2;
  /* The count is in words for this record. */
  if (md_pkt_len + 2 + words + 1 > MD_PKT_WORDS) return;
  md_pkt[md_pkt_len++] = MD_REC_MSGS;
  md_pkt[md_pkt_len++] = (unsigned short)words;
  p = &md_pkt[md_pkt_len];
  for (i = 0; i < md_nummsgs; i++) {
    const int len = (int)strlen(md_msgs[i]);
    *p++ = (unsigned short)len;
    memset(p, 0, ((len + 1) / 2) * 2);
    memcpy(p, md_msgs[i], len); /* ST-order bytes */
    p += (len + 1) / 2;
  }
  md_pkt_len += words;
  pkt_close();
  md_msgs_dirty = 0;
}

static int send_frame(void) {
  int i;
  unsigned short *p;

  pkt_reset();
  add_deltas();
  /* Leave the frame most of a packet; deltas that got in the way go now. */
  if (md_pkt_len > MD_PKT_WORDS / 2) world_flush();

  add_view();
  for (i = 0; i < md_numweapon; i++) {
    p = pkt_item(MD_REC_WEAPON, MD_WEAPON_WORDS);
    if (p) memcpy(p, md_weapon[i], sizeof(md_weapon[i]));
  }
  pkt_close();
  if (md_overlay[0]) {
    p = pkt_item(MD_REC_OVERLAY, MD_OVERLAY_WORDS);
    if (p) memcpy(p, md_overlay, sizeof(md_overlay));
    pkt_close();
  }
  add_messages();
  add_objects();
  pkt_close();
  md_pkt[md_pkt_len++] = MD_REC_END;

  md_seq++;
  return sidecart_md_write(MD_CMD_FRAME, md_pkt, md_pkt_len * 2, (long)md_seq,
                           (long)md_copied_seq, (long)md_level_serial);
}

/* Wait until the MD has at most `ahead` frames left to finish. */
static int wait_ready(int ahead) {
  long t0 = I_GetTimeMS();
  while ((unsigned short)(md_seq - MD_STATUS[MD_ST_READY_SEQ]) > ahead) {
    if (I_GetTimeMS() - t0 > MD_WAIT_MS) return 0;
  }
  return 1;
}

static void copy_ready(unsigned char *screen) {
  extern void atari_md_copy(const void *src, void *dst, int rows, int bpr);
  const unsigned short seq = MD_STATUS[MD_ST_READY_SEQ];
  int x, y, w, h, buf;

  if (seq == md_copied_seq) return;
  buf = MD_STATUS[MD_ST_READY_BUF];
  x = MD_STATUS[MD_ST_VIEW_X];
  y = MD_STATUS[MD_ST_VIEW_Y];
  w = MD_STATUS[MD_ST_VIEW_W];
  h = MD_STATUS[MD_ST_VIEW_H];
  if (w <= 0 || h <= 0 || w > MD_VIEW_MAX_W || h > MD_VIEW_MAX_H || (x & 15) ||
      x + w > 320 || y + h > 200)
    return;
  atari_md_copy((const void *)(MD_ROM4_BASE +
                               (buf ? MD_FRAME_OFFSET_B : MD_FRAME_OFFSET_A)),
                screen + y * 160 + (x >> 1), h, w >> 1);
  md_copied_seq = seq;
}

static void give_up(const char *why) {
  atari_md_active = 0;
  AddMessage((char *)why, MSG_SYSTEM);
}

#if defined(ATARI_MD_AUTOTEST) && (ATARI_MD_AUTOTEST > 0)
/* Test runs: after a few frames, write the ST's palette registers, the
 * MD's published palette and its status block to C:\MDDEBUG.TXT. */
static void autotest_report(void) {
  static int frames;
  char line[160];
  long fh, ssp;
  unsigned short regs[16];
  int i;

  if (++frames != 50) return;
  ssp = Super(0L);
  for (i = 0; i < 16; i++) regs[i] = ((volatile unsigned short *)0xFF8240L)[i];
  Super((void *)ssp);
  fh = Fcreate("MDDEBUG.TXT", 0);
  if (fh < 0) return;
  strcpy(line, "ST pal:");
  for (i = 0; i < 16; i++) sprintf(line + strlen(line), " %03x", regs[i] & 0xFFF);
  strcat(line, "\r\nMD pal:");
  Fwrite((short)fh, strlen(line), line);
  line[0] = 0;
  for (i = 0; i < 16; i++)
    sprintf(line + strlen(line), " %03x",
            ((volatile unsigned short *)(MD_ROM4_BASE + MD_PALETTE_OFFSET))[i]);
  strcat(line, "\r\nstatus:");
  Fwrite((short)fh, strlen(line), line);
  line[0] = 0;
  for (i = 0; i < MD_STATUS_WORDS; i++) sprintf(line + strlen(line), " %04x", MD_STATUS[i]);
  sprintf(line + strlen(line), "\r\nfailures %u\r\n", md_command_failures);
  Fwrite((short)fh, strlen(line), line);
  Fclose((short)fh);
}
#endif

#if defined(ATARI_MD_AUTOTEST) && (ATARI_MD_AUTOTEST > 0)
/* Every 8th of the first 64 frames, as a Degas .PI1 (resolution word,
 * palette, screen), with or without the MD: tests/emu compares the two. */
void ATARI_MD_AutotestShot(int frame) {
  char name[16];
  unsigned short head[17];
  long fh, ssp;
  int i;

  if (frame <= 0 || frame > 64 || (frame & 7)) return;
  ssp = Super(0L);
  head[0] = 0;
  for (i = 0; i < 16; i++) head[1 + i] = ((volatile unsigned short *)0xFF8240L)[i];
  Super((void *)ssp);
  sprintf(name, "SHOT%03d.PI1", frame);
  fh = Fcreate(name, 0);
  if (fh < 0) return;
  Fwrite((short)fh, sizeof(head), head);
  Fwrite((short)fh, 32000L, Physbase());
  Fclose((short)fh);
}
#endif

int ATARI_MD_FinishUpdate(unsigned char *screen, const unsigned char *pixels) {
  if (!atari_md_active || !md_frame_pending) return 0;
  md_frame_pending = 0;

  sidecart_md_bus_begin();

  /* The MD lost the level (it was reset, or a command went missing)? */
  if (!md_snapshot_needed &&
      (MD_STATUS[MD_ST_LEVEL_STATE] != MD_LEVEL_READY ||
       MD_STATUS[MD_ST_LEVEL_SEQ] != (unsigned short)md_level_serial)) {
    md_snapshot_needed = 1;
  }
  if (md_snapshot_needed) {
    if (!send_snapshot()) {
      sidecart_md_bus_end();
      if (++md_snapshot_tries >= 3) give_up("ROTT Accelerator failed: ST renderer");
      return 0;
    }
    md_snapshot_tries = 0;
  }

  /* Pipelined: keep at most one frame in flight before sending another. */
  if (ATARI_MD_PIPELINE) wait_ready(1);
  if (send_frame()) {
    if (++md_failures >= 8) {
      sidecart_md_bus_end();
      give_up("ROTT Accelerator lost: ST renderer");
      return 0;
    }
  } else {
    md_failures = 0;
  }

  /* The HUD the ST drew, around the view; then the view. */
  atari_c2p_hud(screen, pixels, md_view_x, md_view_y, md_view_w, md_view_h);
  if (!ATARI_MD_PIPELINE) wait_ready(0);
  copy_ready(screen);
#if defined(ATARI_MD_AUTOTEST) && (ATARI_MD_AUTOTEST > 0)
  autotest_report();
#endif

  sidecart_md_bus_end();
  return 1;
}

void ATARI_MD_SyncMapSeen(void) {
  volatile const unsigned short *bits =
      (volatile const unsigned short *)(MD_ROM4_BASE + MD_MAPSEEN_OFFSET);
  int w, b;
  if (!atari_md_active) return;
  sidecart_md_bus_begin();
  for (w = 0; w < MD_BITSET_WORDS; w++) {
    const unsigned short v = bits[w];
    if (!v) continue;
    for (b = 0; b < 16; b++) {
      if ((v >> b) & 1) {
        const int t = (w << 4) | b;
        mapseen[t >> 7][t & 127] = 1;
      }
    }
  }
  sidecart_md_bus_end();
}

#endif /* __MINT__ && ATARI_MD_RENDER */
