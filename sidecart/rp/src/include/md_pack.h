/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * File: md_pack.h
 * Description: Level packs (md_pack_format.h) in the PACK_FLASH window.
 *
 * A whole episode's render assets are ~3.8 MB against a ~0.9 MB window,
 * so each level gets its own pack. The ST sends the lumps the level needs
 * -- ROTT's own precache list, which the Atari build still assembles even
 * though it skips the loading -- and the MD copies them out of the WAD on
 * its SD card into flash, unless the pack already holds them. The
 * renderer then reads lumps in place over XIP.
 *
 * One level can still ask for more than fits, so the pack is filled in
 * order of how often a lump is read (md_pack_plan_t) and the rest of the
 * window becomes a ring for lumps loaded on demand when first drawn; see
 * md_pack.c.
 *
 * Programming flash parks Core 1 and turns interrupts off around each
 * flash operation; the ST's ROM4 reads are served by DMA meanwhile, and
 * ROM3 samples wait in the capture ring.
 */

#ifndef MD_PACK_H
#define MD_PACK_H

#include <stdbool.h>
#include <stdint.h>

/* Index a pack that survived in flash from an earlier run, if any. */
void md_pack_init(void);

/* Check the WAD the ST named against the one on the SD card. Returns 0 or
 * MD_ERR_* bits. */
uint16_t md_pack_open_wad(const char *folder, const char *name,
                          uint16_t numlumps, uint32_t size, uint32_t dirofs);

/* True if the current pack holds every lump set in `bitset` (lumps
 * 0..numlumps-1) that the WAD has data for. */
bool md_pack_has_all(const uint16_t *bitset, unsigned numlumps);

/* Preload order. `must` lumps go in first whether asked for or not (the
 * renderer keeps pointers to them for the whole level); then lumps below
 * `shape_first` (walls, doors, flats, sky); then sprites; weapon frames,
 * [guns_first, guns_end), last. */
typedef struct {
  const uint16_t *must;
  unsigned must_count;
  uint16_t shape_first;
  uint16_t guns_first;
  uint16_t guns_end;
} md_pack_plan_t;

/* Build a pack of the lumps in `bitset` from the WAD. `work` is scratch
 * RAM (at least 48 KB; the frame's chunky buffer is lent). `progress` gets
 * 0..100 between flash operations. Returns 0 or MD_ERR_* bits. */
typedef void (*md_pack_progress_t)(unsigned percent);
uint16_t md_pack_build(const uint16_t *bitset, unsigned numlumps,
                       const md_pack_plan_t *plan, uint8_t *work,
                       uint32_t work_size, md_pack_progress_t progress);

/* Use the pack as it stands for a new level (md_pack_has_all was true):
 * empties the demand ring, erasing it. */
void md_pack_reuse(md_pack_progress_t progress);

/* Lump data in flash, or NULL if it cannot be had. Loads the lump from
 * the SD card if the pack lacks it and there is erased room; otherwise
 * queues it for md_pack_service(). The pointer is good until the next
 * md_pack_service() call. */
const uint8_t *md_pack_lump(int lump);
uint32_t md_pack_lump_size(int lump);

/* Preloaded lumps only: the pointer is good until the next build. */
const uint8_t *md_pack_lump_fixed(int lump);

/* Between frames: load the lumps the last frame could not, erasing ring
 * sectors (and dropping the lumps in them) as needed. Stops early when
 * `busy` returns true. */
void md_pack_service(bool (*busy)(void));

/* Demand-loader counters, for the status block. */
extern volatile uint32_t md_pack_loads;
extern volatile uint32_t md_pack_evicts;
extern volatile uint32_t md_pack_fails;

unsigned md_pack_count(void);
unsigned md_pack_missing(void);
unsigned md_pack_kb(void);

#endif /* MD_PACK_H */
