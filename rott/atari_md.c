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

#include <mint/cookie.h>
#include <mint/osbind.h>
#include <stdio.h>
#include <string.h>

#include "atari_c2p.h"
#include "atari_megaste.h"
#include "atari_perf.h"
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
#ifndef ATARI_MD_BLIT
#define ATARI_MD_BLIT 1
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
static unsigned short md_caps; /* MD_ST_CAPS, read at HELLO */
static int md_mste;            /* a Mega STE: its cache in the way */
int atari_md_masked_dirty = 1;

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
static int md_blit_ok; /* frames are copied by the blitter */
static void md_blit_init(void);
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
  md_caps = MD_STATUS[MD_ST_CAPS]; /* 0 from an older firmware */
  sidecart_md_bus_end();
  md_mste = is_megaste();

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
  md_blit_init();
  printf("%s%s\n", result, md_blit_ok ? " (blitter)" : "");
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
  {
    /* the plain C2P path, just this once, and not zoomed (which cut the
     * text off) */
    const int zoom = atari_view_zoom;
    atari_view_zoom = 0;
    atari_md_active = 0;
    I_FinishUpdate();
    atari_md_active = 1;
    atari_view_zoom = zoom;
  }
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
  if (!atari_view_zoom)
    zoom = 1;
  else if (viewwidth <= 80)
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

/* The MD's spotvis of its newest frame, copied out of ROM4 once a frame
 * so the object loop reads RAM with the cache on, with the 5 x 5 tiles
 * around the player added: an object is sent if its tile's bit is set,
 * one test where it was a bitset look and a distance check. */
static unsigned short md_spotvis[MD_BITSET_WORDS];
/* The columns (x) of md_spotvis that may hold bits: the rest are 0. */
static int md_spot_first, md_spot_last = MAPSIZE - 1;

/* Columns first..last of md_spotvis, 8 words each, cleared. */
static void spotvis_clear(int first, int last) {
  if (first <= last)
    memset(md_spotvis + first * 8, 0, (last - first + 1) * 16);
}

/* The columns of the MD's SPOTVIS that svx says hold bits (all of them
 * from older firmware), and those that held some here and don't now
 * cleared. */
static void spotvis_copy(unsigned short svx) {
  int first = 0, last = MAPSIZE - 1;

  if (svx & MD_SVX_VALID) {
    first = MD_SVX_FIRST(svx);
    last = MD_SVX_LAST(svx);
  }
  if (first > last) {
    spotvis_clear(md_spot_first, md_spot_last);
    md_spot_first = MAPSIZE;
    md_spot_last = -1;
    return;
  }
  spotvis_clear(md_spot_first, (first <= md_spot_last ? first : md_spot_last + 1) - 1);
  spotvis_clear(last >= md_spot_first ? last + 1 : md_spot_first, md_spot_last);
  memcpy(md_spotvis + first * 8,
         (const void *)(MD_ROM4_BASE + MD_SPOTVIS_OFFSET + first * 16),
         (last - first + 1) * 16);
  md_spot_first = first;
  md_spot_last = last;
}

static void read_spotvis(void) {
  const int px = player->tilex, py = player->tiley;
  unsigned short svx = MD_STATUS[MD_ST_SPOTVIS_X], again;
  int x, y;

  /* The MD publishes a frame's range before its bits: a new range since
   * the copy began may have new columns in it, so those too. */
  spotvis_copy(svx);
  again = MD_STATUS[MD_ST_SPOTVIS_X];
  if (again != svx) spotvis_copy(again);
  /* the 5 x 5 tiles around the player, the bounds outside the loops (as
   * tests inside them, -O3 unrolled them into 8KB) */
  {
    const int x0 = px - 2 < 0 ? 0 : px - 2;
    const int x1 = px + 2 >= MAPSIZE ? MAPSIZE - 1 : px + 2;
    const int y0 = py - 2 < 0 ? 0 : py - 2;
    const int y1 = py + 2 >= MAPSIZE ? MAPSIZE - 1 : py + 2;
    for (x = x0; x <= x1; x++) {
      unsigned short *const col = md_spotvis + x * 8;
      for (y = y0; y <= y1; y++)
        col[y >> 4] |= (unsigned short)(1u << (y & 15));
    }
    /* what may hold bits now: the columns copied and the player's */
    if (md_spot_first > x0) md_spot_first = x0;
    if (md_spot_last < x1) md_spot_last = x1;
  }
}

/* Is the tile at (x, y), one of its eight neighbours or the player near?
 * The MD publishes each tile with its neighbours' bits already ORed in.
 * Tile i = (x << 7) | y is bit i & 15 of word i >> 4, which on the ST is
 * bit i & 7 of byte (i >> 3) ^ 1: a byte look and a test. Every object
 * is on the map (x, y < MAPSIZE). */
static __inline__ int spotvis_near(unsigned x, unsigned y) {
  const unsigned short i = (unsigned short)((x << 7) | y);
  return (((const unsigned char *)md_spotvis)[(unsigned short)((i >> 3) ^ 1)] >>
          (i & 7)) & 1;
}

int ATARI_MD_SpotvisNear(int x, int y) {
  return (unsigned)x < MAPSIZE && (unsigned)y < MAPSIZE && spotvis_near(x, y);
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
  objtype *const me = player;
  statobj_t *statptr;
  objtype *obj;
  int count = 0;

  for (statptr = firstactivestat; statptr && count < MD_MAX_OBJS;
       statptr = statptr->nextactive) {
    int shapenum, flags, extra = 0;
    unsigned short *p;

    if (statptr->shapenum == NOTHING) continue;
    if (!spotvis_near(statptr->tilex, statptr->tiley)) {
      if (statptr->flags & FL_VISIBLE) statptr->flags &= ~FL_VISIBLE;
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

    if (obj == me) continue;
    if (obj->shapenum == NOTHING) continue;
    if (!spotvis_near(obj->tilex, obj->tiley)) {
      if (obj->flags & FL_VISIBLE) obj->flags &= ~FL_VISIBLE;
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

/* Does the MD's copy differ? */
static int door_changed(int i) {
  const doorobj_t *d = doorobjlist[i];
  const md_door_shadow_t *s = &md_doors[i];
  return d->texture != s->texture || d->alttexture != s->alttexture ||
         (byte)d->action != s->action || d->flags != s->flags;
}

static int mwall_changed(int i) {
  const maskedwallobj_t *m = maskobjlist[i];
  const md_mwall_shadow_t *s = &md_mwalls[i];
  return m->flags != s->flags || m->toptexture != s->top ||
         m->midtexture != s->mid || m->bottomtexture != s->bottom;
}

static int pwall_changed(int i) {
  const pwallobj_t *w = pwallobjlist[i];
  const md_pwall_shadow_t *s = &md_pwalls[i];
  return w->x != s->x || w->y != s->y || w->texture != s->texture ||
         (byte)w->action != s->action;
}

#define MD_ROLL 2         /* doors and moving walls compared a frame */
#define MD_ROLL_MASKED 8  /* masked walls compared a frame */
static int md_roll_door, md_roll_pwall, md_roll_mwall;

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
  /* A door or moving wall at rest whose action has not changed has not
   * changed (its texture and position only move with it), but for the odd
   * flag (an elevator door locked): those moving or changed are compared
   * in full, and the rest MD_ROLL a frame in turn. */
  {
    doorobj_t *const *d = doorobjlist;
    const md_door_shadow_t *sh = md_doors;
    for (i = 0; i < doornum; i++, d++, sh++) {
      const int a = (*d)->action;
      if ((a == dr_opening || a == dr_closing || (byte)a != sh->action) &&
          door_changed(i))
        put_door(world_item(MD_REC_DOOR, MD_DOOR_WORDS), i);
    }
  }
  for (i = 0; i < MD_ROLL && i < doornum; i++) {
    if (++md_roll_door >= doornum) md_roll_door = 0;
    if (door_changed(md_roll_door))
      put_door(world_item(MD_REC_DOOR, MD_DOOR_WORDS), md_roll_door);
  }
  /* Masked walls rarely change, and the changes in play say so
   * (MD_MASKED_TOUCH): all of them then, and otherwise MD_ROLL_MASKED a
   * frame in turn. */
  if (atari_md_masked_dirty) {
    atari_md_masked_dirty = 0;
    for (i = 0; i < maskednum; i++) {
      if (mwall_changed(i)) put_mwall(world_item(MD_REC_MWALL, MD_MWALL_WORDS), i);
    }
  } else {
    for (i = 0; i < MD_ROLL_MASKED && i < maskednum; i++) {
      if (++md_roll_mwall >= maskednum) md_roll_mwall = 0;
      if (mwall_changed(md_roll_mwall))
        put_mwall(world_item(MD_REC_MWALL, MD_MWALL_WORDS), md_roll_mwall);
    }
  }
  {
    pwallobj_t *const *w = pwallobjlist;
    const md_pwall_shadow_t *sh = md_pwalls;
    for (i = 0; i < pwallnum; i++, w++, sh++) {
      const int a = (*w)->action;
      if ((a == pw_pushing || a == pw_moving || (byte)a != sh->action) &&
          pwall_changed(i))
        put_pwall(world_item(MD_REC_PWALL, MD_PWALL_WORDS), i);
    }
  }
  for (i = 0; i < MD_ROLL && i < pwallnum; i++) {
    if (++md_roll_pwall >= pwallnum) md_roll_pwall = 0;
    if (pwall_changed(md_roll_pwall))
      put_pwall(world_item(MD_REC_PWALL, MD_PWALL_WORDS), md_roll_pwall);
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

static void build_frame(void) {
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
}

static int send_frame(void) {
  return sidecart_md_write(MD_CMD_FRAME, md_pkt, md_pkt_len * 2, (long)md_seq,
                           (long)md_copied_seq, (long)md_level_serial);
}

/* The Help key's overlay (ATARI_MD_GetStats): 200Hz ticks the ST spent
 * waiting for the MD, frames, and the MD's own figures for its frame. */
extern volatile unsigned long atari_hz200_count;
static unsigned long md_stat_t0, md_stat_wait, md_stat_cmd, md_stat_frames;
static unsigned long md_stat_logic_tics, md_stat_logic_hz200;
static int md_stat_fps10, md_stat_wait_ms, md_stat_cmd_ms;
static int md_stat_tics10, md_stat_logic_ms;
static unsigned short md_stat_render_us, md_stat_c2p_us;
/* The MD's counters now, and as they stood when the level began. */
enum { MDC_LOADS, MDC_EVICTS, MDC_DROPS, MDC_CHKERRS, MDC_FRAMES, MDC_COUNT };
static const unsigned char md_stat_word[MDC_COUNT] = {
    MD_ST_LOADS, MD_ST_EVICTS, MD_ST_DROPS, MD_ST_CHKERRS, MD_ST_FRAMES};
static unsigned short md_stat_now[MDC_COUNT], md_stat_base[MDC_COUNT];
static unsigned short md_stat_base_seq, md_stat_base_retries;
static long md_stat_level = -1;

/* Once a frame: the averages, every second. */
static void md_stat_frame(void) {
  const unsigned long now = atari_hz200_count;
  unsigned long dt;

  if (md_stat_t0 == 0) {
    md_stat_t0 = now;
    md_stat_frames = md_stat_wait = 0;
    md_stat_logic_tics = atari_logic_tics;
    md_stat_logic_hz200 = atari_logic_hz200;
    return;
  }
  md_stat_frames++;
  dt = now - md_stat_t0;
  if (dt >= 200 && md_stat_frames) {
    md_stat_fps10 = (int)((md_stat_frames * 2000 + dt / 2) / dt);
    md_stat_wait_ms = (int)(md_stat_wait * 5 / md_stat_frames);
    md_stat_cmd_ms = (int)(md_stat_cmd * 5 / md_stat_frames);
    md_stat_tics10 =
        (int)((atari_logic_tics - md_stat_logic_tics) * 10 / md_stat_frames);
    md_stat_logic_ms =
        (int)((atari_logic_hz200 - md_stat_logic_hz200) * 5 / md_stat_frames);
    md_stat_t0 = now;
    md_stat_frames = md_stat_wait = md_stat_cmd = 0;
    md_stat_logic_tics = atari_logic_tics;
    md_stat_logic_hz200 = atari_logic_hz200;
  }
}

/* With the cartridge bus open, once a frame: the MD's figures, the counts
 * starting again from where they stand when a new level begins. */
static void md_stat_read(void) {
  int i;

  md_stat_render_us = MD_STATUS[MD_ST_RENDER_US];
  md_stat_c2p_us = MD_STATUS[MD_ST_C2P_US];
  for (i = 0; i < MDC_COUNT; i++) md_stat_now[i] = MD_STATUS[md_stat_word[i]];
  if (md_stat_level != md_level_serial) {
    md_stat_level = md_level_serial;
    for (i = 0; i < MDC_COUNT; i++) md_stat_base[i] = md_stat_now[i];
    md_stat_base_seq = (unsigned short)md_seq;
    md_stat_base_retries = md_command_retries;
  }
}

void ATARI_MD_GetStats(atari_md_stats_t *s) {
#define MDC(i) ((unsigned short)(md_stat_now[i] - md_stat_base[i]))
  s->fps10 = md_stat_fps10;
  s->tics10 = md_stat_tics10;
  s->logic_ms = md_stat_logic_ms;
  s->wait_ms = md_stat_wait_ms;
  s->cmd_ms = md_stat_cmd_ms;
  s->render_ms = (md_stat_render_us + 500) / 1000;
  s->c2p_ms = (md_stat_c2p_us + 500) / 1000;
  s->sd_loads = MDC(MDC_LOADS);
  s->evicts = MDC(MDC_EVICTS);
  s->drops = MDC(MDC_DROPS);
  s->chkerrs = MDC(MDC_CHKERRS);
  s->retries = (unsigned short)(md_command_retries - md_stat_base_retries);
  s->dups = (short)(MDC(MDC_FRAMES) - (unsigned short)((unsigned short)md_seq - md_stat_base_seq));
#undef MDC
}

/* Wait until the MD has at most `ahead` frames left to finish. */
static int wait_ready(int ahead) {
  long t0 = I_GetTimeMS();
  const unsigned long w0 = atari_hz200_count;
  int ok = 1;
  while ((unsigned short)(md_seq - MD_STATUS[MD_ST_READY_SEQ]) > ahead) {
    if (I_GetTimeMS() - t0 > MD_WAIT_MS) {
      ok = 0;
      break;
    }
  }
  md_stat_wait += atari_hz200_count - w0;
  return ok;
}

/* ------------------------------------------------------------------ */
/* Frame copy by the blitter                                            */
/* ------------------------------------------------------------------ */
/* On an STE or Mega STE the blitter copies the MD's frame to the screen
 * while the CPU goes on with the next frame, the two sharing the bus (blit
 * mode: 64 bus cycles each, the CPU running from its cache where it can),
 * rather than the CPU stopping for the ~13 ms copy. Whatever next touches
 * the cartridge or the screen waits for it first (ATARI_MD_BlitWait). Only
 * used if a test copy by the blitter reads ROM4 the same as the CPU does
 * (md_blit_init). */

#ifndef C__CPU
#define C__CPU 0x5f435055L /* '_CPU' */
#endif
#define BLT_B(off) (*(volatile unsigned char *)(0xFFFF8A00UL + (off)))
#define BLT_W(off) (*(volatile unsigned short *)(0xFFFF8A00UL + (off)))
#define BLT_L(off) (*(volatile unsigned long *)(0xFFFF8A00UL + (off)))
#define BLT_BUSY 0x80

static int md_blit_pending;

/* Where the last frame went on the screen (ATARI_MD_ViewToChunky). */
static int md_shown_x, md_shown_y, md_shown_w, md_shown_h;

/* `rows` rows of `bpr` bytes from src, to rows 160 bytes apart at dst. */
static void md_blit_start(const void *src, void *dst, int rows, int bpr) {
  const long token = sidecart_md_super_force();
  BLT_W(0x20) = 2; /* source x and y increments: rows are contiguous */
  BLT_W(0x22) = 2;
  BLT_L(0x24) = (unsigned long)src;
  BLT_W(0x28) = 0xFFFF; /* end masks: whole words */
  BLT_W(0x2A) = 0xFFFF;
  BLT_W(0x2C) = 0xFFFF;
  BLT_W(0x2E) = 2;
  BLT_W(0x30) = (unsigned short)(160 - bpr + 2);
  BLT_L(0x32) = (unsigned long)dst;
  BLT_W(0x36) = (unsigned short)(bpr >> 1);
  BLT_W(0x38) = (unsigned short)rows;
  BLT_B(0x3A) = 2; /* HOP: source */
  BLT_B(0x3B) = 3; /* OP: source */
  BLT_B(0x3D) = 0; /* no skew */
  BLT_B(0x3C) = BLT_BUSY; /* go, in blit (bus sharing) mode */
  md_blit_pending = 1;
  sidecart_md_super_end(token);
}

void ATARI_MD_BlitWait(void) {
  long token;
  if (!md_blit_pending) return;
  token = sidecart_md_super_force();
  while (BLT_B(0x3C) & BLT_BUSY) {
  }
  sidecart_md_super_end(token);
  md_blit_pending = 0;
}

static void md_blit_init(void) {
  static unsigned short probe[32];
  volatile const unsigned short *rom = (volatile const unsigned short *)MD_ROM4_BASE;
  long cpu = 0;
  int i, same = 1;

  md_blit_ok = 0;
  if (!ATARI_MD_BLIT) return;
  if (!(Blitmode(-1) & 2)) return; /* no blitter */
  /* The supervisor switches use MOVE from SR, a 68000-only freedom. */
  if (Getcookie(C__CPU, &cpu) == C_FOUND && cpu >= 10) return;
  memset(probe, 0, sizeof(probe));
  sidecart_md_bus_begin();
  md_blit_start((const void *)MD_ROM4_BASE, probe, 1, (int)sizeof(probe));
  ATARI_MD_BlitWait();
  for (i = 0; i < 32; i++)
    if (probe[i] != rom[i]) same = 0;
  sidecart_md_bus_end();
  md_blit_ok = same;
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
  if (md_blit_ok)
    md_blit_start((const void *)(MD_ROM4_BASE +
                                 (buf ? MD_FRAME_OFFSET_B : MD_FRAME_OFFSET_A)),
                  screen + y * 160 + (x >> 1), h, w >> 1);
  else
    atari_md_copy((const void *)(MD_ROM4_BASE +
                                 (buf ? MD_FRAME_OFFSET_B : MD_FRAME_OFFSET_A)),
                  screen + y * 160 + (x >> 1), h, w >> 1);
  md_copied_seq = seq;
  md_shown_x = x;
  md_shown_y = y;
  md_shown_w = w;
  md_shown_h = h;
}

void ATARI_MD_ViewToChunky(unsigned char *chunky) {
  if (!atari_md_active || md_shown_w <= 0) return;
  ATARI_MD_BlitWait();
  atari_c2p_screen_to_chunky((const unsigned char *)Physbase(), chunky, md_shown_x,
                             md_shown_y, md_shown_w, md_shown_h);
}

/* ------------------------------------------------------------------ */
/* Music on the MD (MD_CAP_MUSIC)                                       */
/* ------------------------------------------------------------------ */

#define MSTE_CTL (*(volatile unsigned char *)0xFFFF8E21UL)
#define YM_SELECT (*(volatile unsigned char *)0xFFFF8800UL)
#define YM_DATA (*(volatile unsigned char *)0xFFFF8802UL)

static unsigned short md_ym_last; /* the last step played (0: none yet) */
static unsigned char md_ym_shadow[14];
static volatile unsigned char md_ym_playing;

#ifndef ATARI_MD_MUSIC
#define ATARI_MD_MUSIC 1
#endif

int ATARI_MD_MusicAvailable(void) {
  return ATARI_MD_MUSIC && atari_md_active && (md_caps & MD_CAP_MUSIC);
}

int ATARI_MD_Music(int action, int lump, int loop, int volume) {
  if (!ATARI_MD_MusicAvailable()) return 0;
  if (action == MD_MUSIC_PLAY) {
    /* every register written from the first step of the song on (all of
     * it compared, not just its changes) */
    memset(md_ym_shadow, 0xFF, sizeof(md_ym_shadow));
    md_ym_last = 0;
    md_ym_playing = 1; /* until the MD says otherwise */
  }
  return sidecart_md_command(MD_CMD_MUSIC,
                             ((long)action << 16) | (long)(lump & 0xFFFF),
                             ((long)(loop ? 1 : 0) << 8) | (long)(volume & 0xFF)) == 0;
}

int ATARI_MD_MusicPlaying(void) { return md_ym_playing; }

/* The lowest set bit of a byte (md_ym_step's walk of a register mask). */
static const unsigned char md_lsb[256] = {
#define L4(n) n, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0
    0, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, L4(4), L4(5), L4(4),
    L4(6), L4(4), L4(5), L4(4), L4(7), L4(4), L4(5), L4(4), L4(6), L4(4),
    L4(5), L4(4)
#undef L4
};

/* One step from its slot (step is MD_YM_STEP(n)): its changed registers,
 * or all of them, read out, its number read again (the MD may have come
 * round to the slot meanwhile), then those that differ from the chip
 * written to it. 0 if the slot no longer held the step. */
static int md_ym_step(unsigned short step, int all) {
  volatile const unsigned short *slot =
      (volatile const unsigned short *)(MD_ROM4_BASE + MD_YM_SLOT_OFFSET) +
      (step % MD_YM_SLOTS) * MD_YM_WORDS;
  unsigned char reg[14], val[14];
  unsigned short flags;
  unsigned m;
  int n = 0, k;

  if (slot[MD_YM_SEQ] != step) return 0;
  flags = slot[MD_YM_FLAGS];
  for (m = all ? 0x3FFF : MD_YMF_CHANGED(flags); m; m &= m - 1) {
    const int i = (m & 0xFF) ? md_lsb[m & 0xFF] : 8 + md_lsb[m >> 8];
    reg[n] = (unsigned char)i;
    val[n++] = (unsigned char)slot[MD_YM_REGS + i];
  }
  if (slot[MD_YM_SEQ] != step) return 0;
  md_ym_playing = (unsigned char)(flags & MD_YMF_PLAYING);
  for (k = 0; k < n; k++) {
    const int i = reg[k];
    unsigned char b = val[k];
    unsigned short sr;
    if (b == md_ym_shadow[i]) continue;
    md_ym_shadow[i] = b;
    __asm__ volatile("move.w %%sr,%0\n\tori.w #0x0700,%%sr" : "=d"(sr) : : "cc");
    YM_SELECT = (unsigned char)i;
    if (i == 7) b = (unsigned char)((YM_SELECT & 0xC0) | (b & 0x3F));
    YM_DATA = b;
    __asm__ volatile("move.w %0,%%sr" : : "d"(sr) : "cc");
  }
  return 1;
}

/* The VBL (supervisor): the MD's steps in order (MD_YM_SLOTS), one a VBL,
 * or two while behind so it doesn't stay at the edge of the ring. Just
 * started, too far behind or a step missed: on from the newest, every
 * register compared, as only a step that follows the one played can go by
 * its changes. The cartridge is read with a Mega STE's cache off, as
 * everywhere, put back as it was (the game may have it off just now). R7's
 * port bits stay as the chip has them. */
void ATARI_MD_MusicVbl(void) {
  unsigned short newest;
  unsigned char ctl = 0;
  int k;

  if (md_mste) {
    ctl = MSTE_CTL;
    if (ctl & 1) MSTE_CTL = (unsigned char)(ctl & ~1);
  }
  newest = *(volatile const unsigned short *)(MD_ROM4_BASE + MD_YM_NEWEST_OFFSET);
  for (k = 0; k < 2 && newest != md_ym_last && (newest & 0x8000); k++) {
    unsigned short step = (unsigned short)MD_YM_STEP(md_ym_last + 1);
    int all = 0;
    if (md_ym_last == 0 ||
        ((newest - md_ym_last) & MD_YM_STEP_MASK) >= MD_YM_SLOTS) {
      step = newest;
      all = 1;
    }
    if (!md_ym_step(step, all)) {
      md_ym_last = 0;
      break;
    }
    md_ym_last = step;
  }
  if (md_mste && (ctl & 1)) MSTE_CTL = ctl;
}

static int md_hud_redraw;

static void give_up(const char *why) {
  atari_md_active = 0;
  md_hud_redraw = 1; /* the bars again, without the Accelerator's sign */
  AddMessage((char *)why, MSG_SYSTEM);
}

int ATARI_MD_TakeHudRedraw(void) {
  const int r = md_hud_redraw;
  md_hud_redraw = 0;
  return r;
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
  ATARI_MD_BlitWait();
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
  long ssp;

  if (!atari_md_active || !md_frame_pending) return 0;
  md_frame_pending = 0;
  md_stat_frame();

  /* The frame runs in supervisor mode on a Mega STE, so the cache can be
   * off just while the cartridge is in use (not while building the frame
   * or converting the HUD) without each switch costing traps. */
  ssp = sidecart_md_super_begin();
  ATARI_MD_BlitWait(); /* the last frame may still be on its way */
  sidecart_md_bus_begin();

  /* The MD lost the level (it was reset, or a command went missing)? */
  if (!md_snapshot_needed &&
      (MD_STATUS[MD_ST_LEVEL_STATE] != MD_LEVEL_READY ||
       MD_STATUS[MD_ST_LEVEL_SEQ] != (unsigned short)md_level_serial)) {
    md_snapshot_needed = 1;
  }
  if (md_snapshot_needed) {
    /* Resending it can show progress through the plain C2P path, which
     * calls Super(): out of supervisor mode for that. */
    sidecart_md_bus_end();
    sidecart_md_super_end(ssp);
    sidecart_md_bus_begin();
    if (!send_snapshot()) {
      sidecart_md_bus_end();
      if (++md_snapshot_tries >= 3) give_up("ROTT Accelerator failed: ST renderer");
      return 0;
    }
    md_snapshot_tries = 0;
    sidecart_md_bus_end();
    ssp = sidecart_md_super_begin();
    sidecart_md_bus_begin();
  }

  /* Pipelined: keep at most one frame in flight before sending another. */
  if (ATARI_MD_PIPELINE) wait_ready(1);
  md_stat_read();
  read_spotvis();
  sidecart_md_bus_end();

  build_frame();
  {
    const unsigned long c0 = atari_hz200_count;
    const int failed = send_frame();

    md_stat_cmd += atari_hz200_count - c0;
    if (failed) {
      if (++md_failures >= 8) {
        sidecart_md_super_end(ssp);
        give_up("ROTT Accelerator lost: ST renderer");
        return 0;
      }
    } else {
      md_failures = 0;
    }
  }

  /* The HUD the ST drew, around the view; then the view. */
  atari_c2p_hud(screen, pixels, md_view_x, md_view_y, md_view_w, md_view_h);
  sidecart_md_bus_begin();
  if (!ATARI_MD_PIPELINE) wait_ready(0);
  copy_ready(screen);
  sidecart_md_bus_end();
  sidecart_md_super_end(ssp);
#if defined(ATARI_MD_AUTOTEST) && (ATARI_MD_AUTOTEST > 0)
  sidecart_md_bus_begin();
  autotest_report();
  sidecart_md_bus_end();
#endif
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
