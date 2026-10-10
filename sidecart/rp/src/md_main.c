/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * File: md_main.c
 * Description: MD/ROTT command handling: the level pack, the world mirror,
 *              palettes and frames, published in the ROM4 status block.
 *              Platform-free, so tests/emu can run the same code inside an
 *              emulator; emul.c brings the Pico up and calls it.
 *
 * Commands arrive decoded and acknowledged by the ROM3 interrupt
 * (md_proto.c); md_main_poll() takes them in order and does the work. A
 * FRAME is rendered into whichever frame buffer does not hold the newest
 * published frame, so the ST can be copying that one meanwhile.
 */

#include "md_main.h"

#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "hardware/sync.h"
#include "md_music.h"
#include "md_pack.h"
#include "md_proto.h"
#include "md_video.h"
#include "pico/stdlib.h"
#include "rott/r_local.h"
#include "rott_md_protocol.h"

#ifndef RELEASE_VERSION
#define RELEASE_VERSION "dev"
#endif

/* memmap_rp.ld parks buffers (MD_CART_HOLE) in two holes the protocol
 * leaves in the cartridge window: CART_HOLE_A at $E680-$EFFF, CART_HOLE_B
 * at $F200-$FFFF. */
_Static_assert(MD_FRAME_END <= 0xE680 && MD_TOKEN_OFFSET >= 0xF000,
               "the protocol now uses CART_HOLE_A: see memmap_rp.ld");
_Static_assert(MD_RESULT_OFFSET + MD_RESULT_SIZE <= 0xF200,
               "the protocol now uses CART_HOLE_B: see memmap_rp.ld");

/* The view is rendered here, one PLAYPAL index per pixel, then converted
 * into a ROM4 frame buffer. Lent to md_pack_build as scratch while a level
 * pack is built. */
static uint8_t s_chunky[MD_VIEW_MAX_W * MD_VIEW_MAX_H]
    __attribute__((aligned(4)));

static uintptr_t s_rom_base;
static volatile uint16_t *s_status;
static const char *s_folder = "/rott";
static bool s_sd_ok;

static uint16_t s_errors;
static uint32_t s_level_serial;
static int s_level_state = MD_LEVEL_NONE;
static int s_ready_buf = -1; /* buffer of the newest published frame */
static uint16_t s_frames;

/* ------------------------------------------------------------------ */
/* ROM4 helpers                                                         */
/* ------------------------------------------------------------------ */

static inline void status_set(unsigned word, uint16_t value) {
  s_status[word] = value;
}

static void add_error(uint16_t bits) {
  s_errors |= bits;
  status_set(MD_ST_ERRORS, s_errors);
}

/* The result text, stored so the ST reads it with byte reads: the cart bus
 * swaps the two bytes of each word (see stdoom_write_result). */
static void write_result(const char *text) {
  uint8_t *dst = (uint8_t *)(s_rom_base + MD_RESULT_OFFSET);
  size_t len = strlen(text);
  if (len > MD_RESULT_SIZE - 2) len = MD_RESULT_SIZE - 2;
  memset(dst, 0, MD_RESULT_SIZE);
  for (size_t i = 0; i < len; i++) dst[i ^ 1u] = (uint8_t)text[i];
}

static uint8_t *frame_buffer(int index) {
  return (uint8_t *)(s_rom_base +
                     (index ? MD_FRAME_OFFSET_B : MD_FRAME_OFFSET_A));
}

static void publish_counters(void) {
  status_set(MD_ST_CMDS, (uint16_t)md_proto_cmds);
  status_set(MD_ST_CHKERRS, (uint16_t)md_proto_chkerrs);
  status_set(MD_ST_DROPS, (uint16_t)md_proto_drops);
  status_set(MD_ST_LOADS, (uint16_t)md_pack_loads);
  status_set(MD_ST_EVICTS, (uint16_t)md_pack_evicts);
  status_set(MD_ST_LOAD_FAILS, (uint16_t)md_pack_fails);
  if (md_pack_fails) add_error(MD_ERR_PACK_FULL);
}

/* Make a finished frame the ready one. Geometry first, seq last: the ST
 * polls the seq and then reads the rest. */
static void publish_frame(int buf, uint16_t seq, unsigned x, unsigned y,
                          unsigned w, unsigned h, uint32_t render_us,
                          uint32_t c2p_us) {
  status_set(MD_ST_VIEW_X, (uint16_t)x);
  status_set(MD_ST_VIEW_Y, (uint16_t)y);
  status_set(MD_ST_VIEW_W, (uint16_t)w);
  status_set(MD_ST_VIEW_H, (uint16_t)h);
  status_set(MD_ST_READY_BUF, (uint16_t)buf);
  status_set(MD_ST_RENDER_US, (uint16_t)(render_us > 65535u ? 65535u : render_us));
  status_set(MD_ST_C2P_US, (uint16_t)(c2p_us > 65535u ? 65535u : c2p_us));
  status_set(MD_ST_FRAMES, ++s_frames);
  __dmb();
  status_set(MD_ST_READY_SEQ, seq);
  s_ready_buf = buf;
}

static inline int render_target(void) { return s_ready_buf == 0 ? 1 : 0; }

/* SPOTVIS as published: each tile's bit ORed with its eight neighbours'.
 * A column's 128 tiles are 8 consecutive words, bit y & 15 of word y >> 4,
 * so a tile's vertical neighbours are the next bits up and down and its
 * horizontal ones the same bit 8 words either side. The edge rows and
 * columns pick up nothing from beyond the map, and the ST never asks about
 * them. */
static void spread_column(const uint16_t *c, uint16_t *o) {
  for (unsigned k = 0; k < 8; k++) {
    const uint16_t w = c[k];
    const uint16_t below = (uint16_t)(w << 1) | (k > 0 ? (uint16_t)(c[k - 1] >> 15) : 0);
    const uint16_t above = (uint16_t)(w >> 1) | (k < 7 ? (uint16_t)(c[k + 1] << 15) : 0);
    o[k] = (uint16_t)(w | below | above);
  }
}

static void publish_spotvis(const uint16_t *in, volatile uint16_t *out) {
  /* Each column spread up and down (spread_column), three at a time: the
   * one before, this one and the next, ORed into this one's output. */
  uint16_t cols[3][8];
  uint16_t *prev = cols[0], *cur = cols[1], *next = cols[2];
  memset(prev, 0, sizeof(cols[0]));
  spread_column(in, cur);
  for (unsigned x = 0; x < 128; x++) {
    if (x + 1 < 128)
      spread_column(in + (x + 1) * 8, next);
    else
      memset(next, 0, sizeof(cols[0]));
    for (unsigned k = 0; k < 8; k++)
      out[x * 8 + k] = (uint16_t)(prev[k] | cur[k] | next[k]);
    uint16_t *t = prev;
    prev = cur;
    cur = next;
    next = t;
  }
}

/* The renderer's lump access: the level pack, read in place over XIP,
 * with lumps it lacks loaded from the SD card (md_pack.h). Debug builds
 * say once per level which lumps could not be had -- the first thing to
 * check when something draws as a flat colour or not at all. */
#if defined(_DEBUG) && (_DEBUG != 0)
static uint16_t s_missing_logged[4096 / 16] MD_CART_HOLE("b", "missing_logged");
#endif
const byte *R_Lump(int lump) {
  const byte *p = md_pack_lump(lump);
#if defined(_DEBUG) && (_DEBUG != 0)
  if (!p && lump > 0 && lump < 4096 &&
      !(s_missing_logged[lump >> 4] & (1u << (lump & 15)))) {
    s_missing_logged[lump >> 4] |= (uint16_t)(1u << (lump & 15));
    DPRINTF("R_Lump: lump %d is not in the pack\n", lump);
  }
#endif
  return p;
}

const byte *R_LumpFixed(int lump) { return md_pack_lump_fixed(lump); }

/* ------------------------------------------------------------------ */
/* Commands                                                             */
/* ------------------------------------------------------------------ */

static void cmd_hello(const uint16_t *w, uint32_t n) {
  char text[96];
  uint16_t err = 0;

  if (n < 6 + MD_HELLO_WORDS || md_get32(w) != MD_HELLO_MAGIC) {
    add_error(MD_ERR_BAD_CMD);
    return;
  }
  const uint16_t *b = w + 6;
  char name[MD_HELLO_NAME_WORDS * 2 + 1];
  md_get_bytes((uint8_t *)name, b + MD_HELLO_NAME, MD_HELLO_NAME_WORDS * 2);
  name[sizeof(name) - 1] = 0;

  md_music_command(MD_MUSIC_STOP, 0, false, 0); /* the WAD reopens */
  if (!s_sd_ok) {
    err = MD_ERR_NO_SD;
  } else {
    err = md_pack_open_wad(s_folder, name, b[MD_HELLO_NUMLUMPS],
                           md_get32(b + MD_HELLO_WADSIZE),
                           md_get32(b + MD_HELLO_DIROFS));
  }
  /* Music comes from the WAD, so only with it. */
  status_set(MD_ST_CAPS, md_pack_wad_ok() ? MD_CAP_MUSIC : 0);
  /* A new session: the old errors are history. */
  s_errors = 0;
  status_set(MD_ST_ERRORS, 0);
  if (err) add_error(err);

  if (err & MD_ERR_NO_SD) {
    snprintf(text, sizeof(text), "ROTT Accelerator: no SD card");
  } else if (err & MD_ERR_NO_WAD) {
    snprintf(text, sizeof(text), "ROTT Accelerator: %s/%s missing", s_folder,
             name);
  } else if (err & MD_ERR_WAD_MISMATCH) {
    snprintf(text, sizeof(text), "ROTT Accelerator: %s/%s differs", s_folder,
             name);
  } else {
    snprintf(text, sizeof(text), "ROTT Accelerator %s", RELEASE_VERSION);
  }
  write_result(text);
  s_level_state = MD_LEVEL_NONE;
  status_set(MD_ST_LEVEL_STATE, MD_LEVEL_NONE);
  DPRINTF("HELLO: %s (flags %lx)\n", text, (unsigned long)md_get32(w + 4));
}

static void pack_progress(unsigned percent) {
  md_music_service(); /* the song goes on while the pack is built */
  status_set(MD_ST_PROGRESS, (uint16_t)percent);
}

static void cmd_level_begin(const uint16_t *w, uint32_t n) {
  if (n < 6 + MD_LV_WORDS) {
    add_error(MD_ERR_BAD_CMD);
    return;
  }
  const uint16_t *lv = w + 6;
  const uint32_t lvwords = n - 6;
  unsigned numlumps = md_get32(w + 2);
  const unsigned bitwords = lvwords - MD_LV_WORDS;
  if (numlumps > bitwords * 16u) numlumps = bitwords * 16u;

  s_level_serial = md_get32(w);
#if defined(_DEBUG) && (_DEBUG != 0)
  memset(s_missing_logged, 0, sizeof(s_missing_logged));
#endif
  status_set(MD_ST_LEVEL_SEQ, (uint16_t)s_level_serial);
  R_WorldReset();
  s_level_state = MD_LEVEL_LOADING;
  status_set(MD_ST_PROGRESS, 0);
  status_set(MD_ST_LEVEL_STATE, MD_LEVEL_LOADING);

  uint16_t err = 0;
  if (s_errors & (MD_ERR_NO_SD | MD_ERR_NO_WAD | MD_ERR_WAD_MISMATCH)) {
    err = s_errors;
  } else if (!md_pack_has_all(lv + MD_LV_WORDS, numlumps)) {
    /* What the renderer holds on to for the whole level (R_BeginLevel,
     * R_LumpFixed) must be preloaded. */
    const uint16_t must[] = {
        lv[MD_LV_COLORMAP],
        (uint16_t)(lv[MD_LV_SPECMAPS] + 1u),
    };
    const md_pack_plan_t plan = {
        .must = must,
        .must_count = sizeof(must) / sizeof(must[0]),
        .shape_first = lv[MD_LV_SHAPESTART],
        .guns_first = lv[MD_LV_GUNSSTART],
        .guns_end = lv[MD_LV_ELEVSTART],
    };
    const uint32_t t0 = time_us_32();
    err = md_pack_build(lv + MD_LV_WORDS, numlumps, &plan, s_chunky,
                        sizeof(s_chunky), pack_progress);
    DPRINTF("LEVEL %u: pack built in %lu ms, err %x\n", lv[MD_LV_MAPON],
            (unsigned long)((time_us_32() - t0) / 1000u), err);
  } else {
    md_pack_reuse(pack_progress);
  }
  status_set(MD_ST_PACK_LUMPS, (uint16_t)md_pack_count());
  status_set(MD_ST_PACK_MISSING, (uint16_t)md_pack_missing());
  status_set(MD_ST_PACK_KB, (uint16_t)md_pack_kb());
  status_set(MD_ST_PROGRESS, 100);

  if (err & ~MD_ERR_PACK_FULL) {
    add_error(err);
    write_result("ROTT Accelerator: level pack failed");
    s_level_state = MD_LEVEL_ERROR;
  } else {
    if (err) add_error(err);
    if (!R_BeginLevel(lv, lvwords)) {
      add_error(MD_ERR_PACK_IO);
      s_level_state = MD_LEVEL_ERROR;
    } else {
      s_level_state = MD_LEVEL_PACKED;
    }
  }
  status_set(MD_ST_LEVEL_STATE, (uint16_t)s_level_state);
}

static void cmd_tilemap(const uint16_t *w, uint32_t n) {
  if (n < 6) return;
  const uint32_t first = md_get32(w);
  uint32_t count = md_get32(w + 2);
  if (count > n - 6) count = n - 6;
  if (first >= MAPSIZE * MAPSIZE) return;
  if (first + count > MAPSIZE * MAPSIZE) count = MAPSIZE * MAPSIZE - first;
  memcpy(&tilemap[0][0] + first, w + 6, count * sizeof(uint16_t));
}

static void cmd_world(const uint16_t *w, uint32_t n) {
  if (n < 6) return;
  if (md_get32(w) != s_level_serial) return; /* for a level long gone */
  if (!R_ApplyRecords(w + 6, n - 6)) add_error(MD_ERR_BAD_CMD);
}

static void cmd_level_end(const uint16_t *w, uint32_t n) {
  if (n < 2 || md_get32(w) != s_level_serial) return;
  if (s_level_state == MD_LEVEL_PACKED || s_level_state == MD_LEVEL_READY) {
    s_level_state = MD_LEVEL_READY;
    status_set(MD_ST_TILE_CRC, R_TilemapCrc());
    status_set(MD_ST_LEVEL_STATE, MD_LEVEL_READY);
  }
}

static void publish_palette(void) {
  const uint16_t *st = md_video_st_colors();
  volatile uint16_t *pal = (volatile uint16_t *)(s_rom_base + MD_PALETTE_OFFSET);
  for (int i = 0; i < 16; i++) pal[i] = st[i];
}

static void cmd_palette(const uint16_t *w, uint32_t n) {
  /* The view buffer is idle between frames: RGB at the front, the
   * rebuild's scratch after it. */
  uint8_t *rgb = s_chunky;
  if (n < 6 + 384) {
    add_error(MD_ERR_BAD_CMD);
    return;
  }
  md_get_bytes(rgb, w + 6, 768);
  md_video_set_palette(rgb, md_get32(w + 2), s_chunky + 1024);
  publish_palette();
  status_set(MD_ST_PAL_SEQ, (uint16_t)md_get32(w));
}

static void cmd_frame(const uint16_t *w, uint32_t n) {
  if (n < 6) return;
  const uint16_t seq = (uint16_t)md_get32(w);
  if (s_level_state != MD_LEVEL_READY || md_get32(w + 4) != s_level_serial) {
    add_error(MD_ERR_NO_LEVEL);
    return;
  }

  const uint32_t t0 = time_us_32();
  R_FrameReset();
  if (!R_ApplyRecords(w + 6, n - 6)) {
    add_error(MD_ERR_BAD_CMD);
    return;
  }
  if (viewwidth <= 0 || viewheight <= 0 || viewwidth > MD_VIEW_MAX_W ||
      viewheight > MD_VIEW_MAX_H || (viewwidth & 15)) {
    add_error(MD_ERR_BAD_CMD);
    return;
  }
  R_RenderFrame(s_chunky);
  const uint32_t t1 = time_us_32();

  const int buf = render_target();
  md_video_c2p(s_chunky, R_PITCH, frame_buffer(buf), (unsigned)viewwidth,
               (unsigned)viewheight, r_view_screen_y);
  publish_spotvis(r_frame_bits,
                  (volatile uint16_t *)(s_rom_base + MD_SPOTVIS_OFFSET));
  const uint32_t t2 = time_us_32();

  publish_frame(buf, seq, r_view_screen_x, r_view_screen_y,
                (unsigned)viewwidth, (unsigned)viewheight, t1 - t0, t2 - t1);

#if defined(_DEBUG) && (_DEBUG != 0)
  static uint32_t s_render_max, s_c2p_max, s_gap_max, s_last;
  if (t1 - t0 > s_render_max) s_render_max = t1 - t0;
  if (t2 - t1 > s_c2p_max) s_c2p_max = t2 - t1;
  if (s_last && t0 - s_last > s_gap_max) s_gap_max = t0 - s_last;
  s_last = t0;
  if ((s_frames & 63u) == 0) {
    DPRINTF("frame %u: render %lu (max %lu) c2p %lu (max %lu) us, gap max "
            "%lu us, cmds %lu chk %lu drop %lu, objs %d\n",
            s_frames, (unsigned long)(t1 - t0), (unsigned long)s_render_max,
            (unsigned long)(t2 - t1), (unsigned long)s_c2p_max,
            (unsigned long)s_gap_max, (unsigned long)md_proto_cmds,
            (unsigned long)md_proto_chkerrs, (unsigned long)md_proto_drops,
            r_numobjs);
    s_render_max = s_c2p_max = s_gap_max = 0;
  }
#endif
}

/* M1: a test pattern published exactly like a frame, to measure and
 * check the transport and the ST's copy without the renderer. */
static void cmd_test(const uint16_t *w, uint32_t n) {
  if (n < 6 + MD_TEST_WORDS) return;
  const uint16_t seq = (uint16_t)md_get32(w);
  const uint32_t pattern = md_get32(w + 2);
  const uint32_t wh = md_get32(w + 4);
  unsigned width = wh >> 16;
  unsigned height = wh & 0xFFFFu;
  const unsigned sx = w[6 + MD_TEST_SCREENX];
  const unsigned sy = w[6 + MD_TEST_SCREENY];
  if (width > MD_VIEW_MAX_W) width = MD_VIEW_MAX_W;
  if (height > MD_VIEW_MAX_H) height = MD_VIEW_MAX_H;
  width &= ~15u;

  const uint32_t t0 = time_us_32();
  for (unsigned y = 0; y < height; y++) {
    uint8_t *row = s_chunky + y * R_PITCH;
    for (unsigned x = 0; x < width; x++) {
      uint8_t v;
      if (x < 2 || y < 2 || x >= width - 2 || y >= height - 2) {
        v = 255; /* frame */
      } else if (((x + seq * 2u) % width) < 8u) {
        v = 255; /* a bar sweeping right, one step per frame */
      } else if (pattern & 1u) {
        v = (uint8_t)((x * 256u) / width); /* horizontal ramp */
      } else {
        v = (uint8_t)(((x >> 4) + (y >> 4)) & 1u ? 200 : 40); /* checks */
      }
      row[x] = v;
    }
  }
  const uint32_t t1 = time_us_32();
  const int buf = render_target();
  md_video_c2p(s_chunky, R_PITCH, frame_buffer(buf), width, height, sy);
  const uint32_t t2 = time_us_32();
  publish_frame(buf, seq, sx, sy, width, height, t1 - t0, t2 - t1);
}

static void cmd_music(const uint16_t *w, uint32_t n) {
  if (n < 4) return;
  const uint32_t d3 = md_get32(w), d4 = md_get32(w + 2);
  md_music_command((unsigned)(d3 >> 16), (int)(d3 & 0xFFFFu),
                   ((d4 >> 8) & 0xFFu) != 0, (unsigned)(d4 & 0xFFu));
}

static void cmd_echo(const uint16_t *w, uint32_t n) {
  static uint16_t s_ok, s_bad;
  if (n < 6) return;
  uint32_t count = md_get32(w + 4);
  if (count > n - 6) {
    status_set(MD_ST_ECHO_BAD, ++s_bad);
    return;
  }
  uint32_t sum = 0;
  for (uint32_t i = 0; i < count; i++) sum += w[6 + i];
  if (sum == md_get32(w + 2)) {
    status_set(MD_ST_ECHO_OK, ++s_ok);
  } else {
    status_set(MD_ST_ECHO_BAD, ++s_bad);
  }
}

static void dispatch(const md_cmd_t *c) {
  const uint16_t *w = md_cmd_words(c);
  const uint32_t n = md_cmd_word_count(c);

  switch (c->command_id) {
    case MD_CMD_HELLO:
      cmd_hello(w, n);
      break;
    case MD_CMD_IDLE:
      break;
    case MD_CMD_LEVEL_BEGIN:
      cmd_level_begin(w, n);
      break;
    case MD_CMD_TILEMAP:
      cmd_tilemap(w, n);
      break;
    case MD_CMD_WORLD:
      cmd_world(w, n);
      break;
    case MD_CMD_LEVEL_END:
      cmd_level_end(w, n);
      break;
    case MD_CMD_PALETTE:
      cmd_palette(w, n);
      break;
    case MD_CMD_FRAME:
      cmd_frame(w, n);
      break;
    case MD_CMD_TEST:
      cmd_test(w, n);
      break;
    case MD_CMD_MUSIC:
      cmd_music(w, n);
      break;
    case MD_CMD_ECHO:
      cmd_echo(w, n);
      break;
    default:
      add_error(MD_ERR_BAD_CMD);
      break;
  }
  /* Only once it has run: the ST waits for HELLO here, then reads what
   * it set (the result, the errors, the capabilities). */
  __dmb();
  status_set(MD_ST_LAST_CMD, c->command_id);
}

/* ------------------------------------------------------------------ */

void md_main_init(uintptr_t rom_base, const char *folder, bool sd_ok) {
  s_rom_base = rom_base;
  s_folder = folder;
  s_sd_ok = sd_ok;
  s_status = (volatile uint16_t *)(s_rom_base + MD_STATUS_OFFSET);

  status_set(MD_ST_MAGIC, MD_STATUS_MAGIC);
  status_set(MD_ST_PROTO, MD_PROTOCOL_VERSION);
  status_set(MD_ST_LEVEL_STATE, MD_LEVEL_NONE);
  write_result("ROTT Accelerator " RELEASE_VERSION);
  r_mapseen_bits = (uint16_t *)(s_rom_base + MD_MAPSEEN_OFFSET);
  if (!sd_ok) add_error(MD_ERR_NO_SD);

  /* Until the ST sends ROTT's palette: a grey ramp, so a test pattern has
   * something to dither with. */
  for (int i = 0; i < 256; i++) {
    s_chunky[i * 3] = s_chunky[i * 3 + 1] = s_chunky[i * 3 + 2] = (uint8_t)i;
  }
  md_video_set_palette(s_chunky, 0, s_chunky + 1024);
  publish_palette();
  md_pack_init();
  md_music_init(rom_base);
}

void md_main_ready(void) {
  /* Both bytes of the ready word, so a byte read at either address sees
   * it. */
  *(volatile uint16_t *)(s_rom_base + MD_READY_OFFSET) =
      (uint16_t)((MD_READY_MAGIC << 8) | MD_READY_MAGIC);
  DPRINTF("MD/ROTT %s ready, folder %s\n", RELEASE_VERSION, s_folder);
}

static bool command_waiting(void) { return md_proto_peek() != NULL; }

bool md_main_poll(void) {
  static uint16_t heartbeat;
  status_set(MD_ST_HEARTBEAT, ++heartbeat);
  publish_counters(); /* checksum errors happen without a command */
  md_music_service(); /* a few hundred bytes a second, from the WAD */
  md_cmd_t *c = md_proto_peek();
  if (!c) {
    /* Idle: load what the last frame had to go without. */
    if (s_level_state == MD_LEVEL_READY) md_pack_service(command_waiting);
    return false;
  }
  dispatch(c);
  md_proto_pop();
  publish_counters();
  return true;
}
