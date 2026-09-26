/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * File: uptest.c
 * Description: MD/ROTT milestone M1 frame test (UPTEST.TOS).
 *
 * Needs only the MD/ROTT firmware; no WAD. The MD draws a test pattern
 * (border, checks or a ramp, and a bar that moves one step per frame),
 * dithers it to 16 colours and converts it to ST planes in ROM4, exactly
 * like a game frame; the ST copies it to the screen with ROTT's copy
 * routine. Two geometries (full width, which takes the fast copy path, and
 * a narrower view, row by row), each run twice:
 *   sequential: send, wait for the frame, copy;
 *   pipelined:  send the next frame, then copy the last one while the MD
 *               renders, as ROTT does (ATARI_MD_PIPELINE=1).
 * Reports frames per second, the ST's send/wait/copy times and the MD's
 * render and c2p times. Saves the text as UPTEST.TXT. ST low resolution.
 */

#include "mdtest.h"

#define FRAMES 100
#define WAIT_TICKS 50 /* 250 ms, as ROTT */

extern void atari_md_copy(const void *src, void *dst, int rows, int bpr);

typedef struct {
  int x, y, w, h;
} geom_t;

#define ST_PALETTE ((volatile unsigned short *)0xFF8240L)

static unsigned char s_pal[768];
static unsigned short s_st_pal[16];
static unsigned char *s_screen;

static void get_palette(unsigned short *pal) {
  int i;
  for (i = 0; i < 16; i++) pal[i] = ST_PALETTE[i];
}

static void set_palette(const unsigned short *pal) {
  int i;
  for (i = 0; i < 16; i++) ST_PALETTE[i] = pal[i];
}

/* A grey ramp with pure red at 255, the pattern's border and bar. */
static int send_palette(void) {
  const unsigned long t0 = HZ200;
  int i;
  for (i = 0; i < 256; i++) {
    s_pal[i * 3 + 0] = s_pal[i * 3 + 1] = s_pal[i * 3 + 2] = (unsigned char)i;
  }
  s_pal[255 * 3 + 1] = s_pal[255 * 3 + 2] = 0;
  if (sidecart_md_write(MD_CMD_PALETTE, s_pal, 768, 1L, 0L, 0L)) return 0;
  while (status(MD_ST_PAL_SEQ) != 1) {
    if (HZ200 - t0 > 200) return 0;
  }
  sidecart_md_bus_begin();
  for (i = 0; i < 16; i++) {
    s_st_pal[i] =
        ((volatile unsigned short *)(MD_ROM4_BASE + MD_PALETTE_OFFSET))[i];
  }
  sidecart_md_bus_end();
  set_palette(s_st_pal);
  return 1;
}

static int send_test(unsigned short seq, const geom_t *g) {
  unsigned short buf[MD_TEST_WORDS];
  buf[MD_TEST_SCREENX] = (unsigned short)g->x;
  buf[MD_TEST_SCREENY] = (unsigned short)g->y;
  return sidecart_md_write(MD_CMD_TEST, buf, sizeof(buf), (long)seq,
                           (long)((seq >> 5) & 1),
                           ((long)g->w << 16) | (long)g->h);
}

static int wait_seq(unsigned short seq) {
  const unsigned long t0 = HZ200;
  while (status(MD_ST_READY_SEQ) != seq) {
    if (HZ200 - t0 > WAIT_TICKS) return 0;
  }
  return 1;
}

static void copy_ready(void) {
  int buf, x, y, w, h;
  sidecart_md_bus_begin();
  buf = MD_STATUS[MD_ST_READY_BUF];
  x = MD_STATUS[MD_ST_VIEW_X];
  y = MD_STATUS[MD_ST_VIEW_Y];
  w = MD_STATUS[MD_ST_VIEW_W];
  h = MD_STATUS[MD_ST_VIEW_H];
  if (w > 0 && h > 0 && !(x & 15) && x + w <= 320 && y + h <= 200) {
    atari_md_copy((const void *)(MD_ROM4_BASE +
                                 (buf ? MD_FRAME_OFFSET_B : MD_FRAME_OFFSET_A)),
                  s_screen + y * 160 + (x >> 1), h, w >> 1);
  }
  sidecart_md_bus_end();
}

typedef struct {
  unsigned long total, send, wait, copy;
  int late, failed;
  unsigned short render_us, c2p_us;
} result_t;

static unsigned short s_seq;

static void run(const geom_t *g, int pipelined, result_t *r) {
  unsigned long t0, t;
  int i;

  memset(r, 0, sizeof(*r));
  memset(s_screen, 0, 32000);
  t0 = HZ200;
  if (pipelined && send_test(++s_seq, g)) r->failed++;
  for (i = 0; i < FRAMES; i++) {
    const unsigned short want = s_seq;
    if (!pipelined) {
      t = HZ200;
      if (send_test(++s_seq, g)) r->failed++;
      r->send += HZ200 - t;
    }
    t = HZ200;
    if (!wait_seq(pipelined ? want : s_seq)) r->late++;
    r->wait += HZ200 - t;
    if (pipelined) {
      t = HZ200;
      if (send_test(++s_seq, g)) r->failed++;
      r->send += HZ200 - t;
    }
    t = HZ200;
    copy_ready();
    r->copy += HZ200 - t;
  }
  if (pipelined) wait_seq(s_seq);
  r->total = HZ200 - t0;
  r->render_us = status(MD_ST_RENDER_US);
  r->c2p_us = status(MD_ST_C2P_US);
}

static void report(const geom_t *g, int pipelined, const result_t *r) {
  const unsigned long total = r->total ? r->total : 1;
  out("%3dx%3d %s: %lu.%lu fps\r\n", g->w, g->h,
      pipelined ? "pipelined " : "sequential",
      (FRAMES * 200UL) / total, ((FRAMES * 2000UL) / total) % 10);
  out("  per frame: send %lu, wait %lu, copy %lu ms\r\n",
      r->send * 5 / FRAMES, r->wait * 5 / FRAMES, r->copy * 5 / FRAMES);
  out("  MD: render %u us, c2p %u us; late %d, failed %d\r\n", r->render_us,
      r->c2p_us, r->late, r->failed);
  if (r->late || r->failed) s_fail = 1;
}

int main(void) {
  static const geom_t geoms[2] = {{0, 16, 320, 168}, {64, 40, 192, 120}};
  static result_t results[2][2];
  unsigned short saved_pal[16];
  long ssp;
  int g, p;

  ssp = Super(0L);
  (void)Cconws("\033E");
  out("ROTT Accelerator UPTEST\r\n\r\n");
  if (Getrez() != 0) {
    out("Needs ST low resolution.\r\n");
    s_fail = 1;
  } else if (!sidecart_md_present()) {
    out("ROTT Accelerator not found (no ready\r\n"
        "magic in ROM4). Is it selected in\r\n"
        "Booster?\r\n");
    s_fail = 1;
  } else if (!first_contact()) {
    out("The firmware does not answer commands.\r\n");
    s_fail = 1;
  } else {
    s_screen = (unsigned char *)Physbase();
    get_palette(saved_pal);
    (void)Cconws("\033f"); /* cursor off */
    if (!send_palette()) {
      out("PALETTE was not taken.\r\n");
      s_fail = 1;
    } else {
      settle();
      s_seq = status(MD_ST_READY_SEQ);
      for (g = 0; g < 2; g++) {
        for (p = 0; p < 2; p++) run(&geoms[g], p, &results[g][p]);
      }
      set_palette(saved_pal);
      (void)Cconws("\033E\033e");
      out("ROTT Accelerator UPTEST\r\n\r\n");
      for (g = 0; g < 2; g++) {
        for (p = 0; p < 2; p++) report(&geoms[g], p, &results[g][p]);
      }
      out("MD palette:");
      for (g = 0; g < 16; g++) out(" %03x", s_st_pal[g] & 0xFFF);
      out("\r\nMD status: errors %04x, checksum errors %u, drops %u\r\n",
          status(MD_ST_ERRORS), status(MD_ST_CHKERRS), status(MD_ST_DROPS));
      if (status(MD_ST_ERRORS)) s_fail = 1;
    }
  }
  out("\r\n%s\r\n", s_fail ? "FAIL" : "PASS");
  log_save("UPTEST.TXT");
  Super((void *)ssp);
  (void)Cconws("Saved UPTEST.TXT. Press a key.\r\n");
  (void)Cconin();
  return s_fail;
}
