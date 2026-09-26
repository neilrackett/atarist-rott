/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * packtest.c - stress the level pack and its demand-loading ring
 * (rp/src/md_pack.c) on the host.
 *
 * Builds a pack for far more lumps than fit (every graphics lump in the
 * WAD), then plays thousands of "frames": each asks for a random handful
 * of lumps, checks every pointer it gets against the WAD at once and
 * again at the end of the frame (a pointer must stay good for the whole
 * frame), then lets md_pack_service() run as the firmware does between
 * frames. Lumps a frame had to go without must be there next time, and
 * the preloaded maps must never move.
 *
 *   packtest <sd_root>      (<sd_root>/rott/HUNTBGIN.WAD)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "md_pack.h"
#include "mdemu.h"
#include "rott_md_protocol.h"

#define FRAMES 3000
#define PER_FRAME 24

static uint8_t *s_wad;
static uint32_t s_wad_size;
static unsigned s_numlumps;
static uint32_t s_dirofs;
static uint8_t s_work[MD_VIEW_MAX_W * MD_VIEW_MAX_H];
static uint16_t s_bits[4096 / 16];

static uint32_t rd32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}
static const uint8_t *dir(unsigned lump) { return s_wad + s_dirofs + 16u * lump; }
static uint32_t lump_size(unsigned lump) { return rd32(dir(lump) + 4); }
static const uint8_t *lump_data(unsigned lump) { return s_wad + rd32(dir(lump)); }

static int find(const char *name) {
  for (unsigned i = 0; i < s_numlumps; i++) {
    if (!strncmp((const char *)dir(i) + 8, name, 8)) return (int)i;
  }
  return -1;
}

static int fail(const char *what, unsigned lump) {
  printf("packtest: FAIL: %s (lump %u)\n", what, lump);
  return 1;
}

int main(int argc, char **argv) {
  char path[1024];
  const char *sd = argc > 1 ? argv[1] : ".";
  snprintf(path, sizeof(path), "%s/rott/HUNTBGIN.WAD", sd);
  FILE *f = fopen(path, "rb");
  if (!f) {
    printf("packtest: cannot open %s\n", path);
    return 1;
  }
  fseek(f, 0, SEEK_END);
  s_wad_size = (uint32_t)ftell(f);
  fseek(f, 0, SEEK_SET);
  s_wad = malloc(s_wad_size);
  if (fread(s_wad, 1, s_wad_size, f) != s_wad_size) return 1;
  fclose(f);
  s_numlumps = rd32(s_wad + 4);
  s_dirofs = rd32(s_wad + 8);

  if (mdemu_init(sd)) return fail("mdemu_init", 0);
  if (md_pack_open_wad("/rott", "HUNTBGIN.WAD", (uint16_t)s_numlumps,
                       s_wad_size, s_dirofs)) {
    return fail("md_pack_open_wad", 0);
  }

  const int digistrt = find("DIGISTRT");
  const int colormap = find("COLORMAP");
  const int specmaps = find("SPECMAPS");
  const int shapstrt = find("SHAPSTRT");
  const int gunstart = find("GUNSTART");
  const int elevstrt = find("ELEVSTRT");
  if (digistrt < 0 || colormap < 0 || specmaps < 0 || shapstrt < 0) {
    return fail("WAD markers", 0);
  }
  unsigned graphics[4096];
  unsigned ngraphics = 0;
  for (int i = 1; i < digistrt; i++) {
    if (!lump_size((unsigned)i)) continue;
    s_bits[i >> 4] |= (uint16_t)(1u << (i & 15));
    graphics[ngraphics++] = (unsigned)i;
  }

  const uint16_t must[] = {(uint16_t)colormap, (uint16_t)(specmaps + 1)};
  const md_pack_plan_t plan = {must, 2, (uint16_t)shapstrt, (uint16_t)gunstart,
                               (uint16_t)elevstrt};
  uint16_t err = md_pack_build(s_bits, s_numlumps, &plan, s_work,
                               sizeof(s_work), NULL);
  if (err) return fail("md_pack_build", err);
  printf("packtest: %u graphics lumps asked for, %u preloaded, %u on demand\n",
         ngraphics, md_pack_count(), md_pack_missing());

  const uint8_t *cm = md_pack_lump_fixed(colormap);
  const uint8_t *red = md_pack_lump_fixed(specmaps + 1);
  if (!cm || !red) return fail("maps not preloaded", (unsigned)colormap);

  srand(1234);
  unsigned got = 0, missed = 0, missed_twice = 0;
  static uint8_t missed_last[4096];
  for (int frame = 0; frame < FRAMES; frame++) {
    const uint8_t *ptr[PER_FRAME];
    unsigned lump[PER_FRAME];
    static uint8_t missed_now[4096];
    memset(missed_now, 0, sizeof(missed_now));
    /* A working set that drifts through the WAD, plus the odd outlier. */
    const unsigned base = (unsigned)(frame / 40) * 17u % ngraphics;
    for (int k = 0; k < PER_FRAME; k++) {
      unsigned idx = (rand() % 8) ? (base + (unsigned)(rand() % 60)) % ngraphics
                                  : (unsigned)(rand() % ngraphics);
      lump[k] = graphics[idx];
      ptr[k] = md_pack_lump((int)lump[k]);
      if (!ptr[k]) {
        missed++;
        if (missed_last[lump[k]]) missed_twice++;
        missed_now[lump[k]] = 1;
        continue;
      }
      got++;
      if (md_pack_lump_size((int)lump[k]) != lump_size(lump[k]) ||
          memcmp(ptr[k], lump_data(lump[k]), lump_size(lump[k]))) {
        return fail("wrong data", lump[k]);
      }
    }
    for (int k = 0; k < PER_FRAME; k++) {
      if (ptr[k] && memcmp(ptr[k], lump_data(lump[k]), lump_size(lump[k]))) {
        return fail("data changed within the frame", lump[k]);
      }
    }
    if (md_pack_lump_fixed(colormap) != cm || memcmp(cm, lump_data(colormap), 8192) ||
        md_pack_lump_fixed(specmaps + 1) != red) {
      return fail("preloaded map moved", (unsigned)colormap);
    }
    md_pack_service(NULL);
    memcpy(missed_last, missed_now, sizeof(missed_last));
  }
  printf("packtest: %d frames, %u lumps drawn, %u went without for a frame "
         "(%u twice running)\n",
         FRAMES, got, missed, missed_twice);
  printf("packtest: loads %u, evictions %u, failures %u\n",
         (unsigned)md_pack_loads, (unsigned)md_pack_evicts,
         (unsigned)md_pack_fails);
  if (md_pack_fails) return fail("lumps that could not be loaded", 0);
  if (!md_pack_evicts) return fail("the ring never wrapped (test too small)", 0);
  printf("packtest: OK\n");
  return 0;
}
