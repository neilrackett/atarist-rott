/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * File: mdfw_app.c
 * Description: The ROTT Accelerator for EmuMD (see ../mdfw.ini):
 *              what emul.c does after the hardware set-up, the main loop,
 *              Core 1 run in line, and optional frame dumps:
 *                --md-option dump=<dir>        write mdNNNNN.ppm frames
 *                --md-option dump-every=<n>    every nth frame (default 1)
 *                --md-option chunky=<dir>      write cNNNNN_WxH.raw views
 */
#include <stdio.h>
#include <string.h>

#include "md_core1.h"
#include "md_main.h"
#include "md_proto.h"
#include "mdfw.h"
#include "rott_md_protocol.h"
#include "target_firmware.h"

/* ---- Core 1: jobs run in line, so the emulator stays deterministic --- */

static md_core1_job_t s_job;
static void *s_job_arg;
void md_core1_init(void) {}
void md_core1_dispatch(md_core1_job_t job, void *arg) {
  s_job = job;
  s_job_arg = arg;
}
void md_core1_wait(void) {
  if (s_job) s_job(s_job_arg);
  s_job = NULL;
}
void md_core1_park(void) {}
void md_core1_unpark(void) {}

/* ---- Frame dumps --------------------------------------------------- */

/* The palette of the last view (mdfw_app_view), for an 8-plane dump. */
static uint8_t s_rgb[768];

/* The published planar frame, decoded through the 16 ST colours, or as
 * 256 (MD_HELLO_EIGHT) through the game's: exactly what the ST copies to
 * its screen. */
static void dump_ready_frame(void) {
  static uint16_t last;
  const int every = mdfw_option_int("dump-every", 1);
  /* Second: mdfw_option's value is only good until the next call. */
  const char *dir = mdfw_option("dump");
  if (!dir) return;
  const uint16_t *rom4 = mdfw_rom4();
  const uint16_t *st = rom4 + MD_STATUS_OFFSET / 2;
  const uint16_t frames = st[MD_ST_FRAMES];
  if (frames == last || (every > 1 && frames % every)) return;
  last = frames;
  const unsigned w = st[MD_ST_VIEW_W], h = st[MD_ST_VIEW_H];
  const uint16_t *pal = rom4 + MD_PALETTE_OFFSET / 2;
  const uint16_t *fb =
      rom4 + (st[MD_ST_READY_BUF] ? MD_FRAME_OFFSET_B : MD_FRAME_OFFSET_A) / 2;
  char name[1024];
  snprintf(name, sizeof(name), "%s/md%05u.ppm", dir, st[MD_ST_READY_SEQ]);
  FILE *f = fopen(name, "wb");
  if (!f) return;
  fprintf(f, "P6\n%u %u\n255\n", w, h);
  if (md_main_eight()) {
    fb = rom4 + MD_FRAME_OFFSET_A / 2;
    for (unsigned y = 0; y < h; y++) {
      for (unsigned x = 0; x < w; x++) {
        const uint16_t *g = fb + y * (w / 2) + (x / 16) * 8;
        const unsigned bit = 15 - (x & 15);
        unsigned c = 0;
        for (unsigned p = 0; p < 8; p++) c |= ((g[p] >> bit) & 1u) << p;
        fwrite(s_rgb + c * 3, 1, 3, f);
      }
    }
    fclose(f);
    return;
  }
  for (unsigned y = 0; y < h; y++) {
    for (unsigned x = 0; x < w; x++) {
      const uint16_t *g = fb + y * (w / 4) + (x / 16) * 4;
      const unsigned bit = 15 - (x & 15);
      unsigned pen = 0;
      for (unsigned p = 0; p < 4; p++) pen |= ((g[p] >> bit) & 1u) << p;
      const uint16_t c = pal[pen];
      for (int k = 0; k < 3; k++) {
        const unsigned n = (c >> (8 - 4 * k)) & 15u;
        fputc((int)((((n & 7u) << 1) | (n >> 3)) * 17u), f);
      }
    }
  }
  fclose(f);
}

/* Each view as the firmware rendered it, before its 16 colours: the
 * 768-byte palette, then w x h pixels, for tools/rottpalette. */
void mdfw_app_view(const uint8_t *rgb768, const uint8_t *chunky, int pitch,
                   int w, int h) {
  static unsigned n;
  memcpy(s_rgb, rgb768, sizeof(s_rgb));
  const char *dir = mdfw_option("chunky");
  if (!dir) return;
  char name[1024];
  snprintf(name, sizeof(name), "%s/c%05u_%dx%d.raw", dir, n++, w, h);
  FILE *f = fopen(name, "wb");
  if (!f) return;
  fwrite(rgb768, 1, 768, f);
  for (int y = 0; y < h; y++) fwrite(chunky + y * pitch, 1, (size_t)w, f);
  fclose(f);
}

/* ---- The firmware -------------------------------------------------- */

static int app_init(void) {
  const uintptr_t rom4 = mdfw_rom4_base();
  mdfw_rom4_load(target_firmware, target_firmware_length);
  md_proto_init(rom4);
  md_main_init(rom4, "/rott", true);
  md_main_ready();
  return 0;
}

const mdfw_app_t mdfw_app = {
    .name = MDFW_NAME,
    .version = MDFW_VERSION,
    .init = app_init,
    .poll = md_main_poll,
    .after_poll = dump_ready_frame,
};
