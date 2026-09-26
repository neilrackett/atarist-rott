/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * File: md_pack.c
 * Description: Level packs in PACK_FLASH. See md_pack.h and
 *              md_pack_format.h. The flash-programming pattern (Core 1
 *              parked, interrupts off per operation, XIP live between them
 *              for the SD reads) is md-doom's pack.c.
 */

#include "md_pack.h"

#include <stdlib.h>
#include <string.h>

#include "constants.h"
#include "debug.h"
#include "ff.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "md_core1.h"
#include "md_pack_format.h"
#include "rott_md_protocol.h"

/* Most lumps one pack can hold (the lump numbers are kept in RAM for the
 * lookup). A shareware level asks for a few hundred. */
#define MD_PACK_MAX_LUMPS 2048

/* Highest WAD lump number the demand loader tracks (the registered WAD
 * has about 3,000). */
#define MD_PACK_MAX_WAD_LUMPS 4096

/* Flash staging chunk, carved from the front of the work buffer. */
#define MD_PACK_STAGE 16384u

/* Flash kept back from preloading, for lumps loaded on demand. */
#define MD_PACK_RING_MIN (192u * 1024u)

/* Lumps the ring can hold at once, and misses one frame can queue. */
#define MD_RING_SLOTS 512
#define MD_DEFER_MAX 32

/* Loads a frame may do itself before the rest wait for md_pack_service,
 * so one frame full of new sprites cannot stall the ST for long. */
#define MD_INLINE_LOADS_PER_FRAME 8

#define WAD_DIR_ENTRY 16u

static uint16_t s_lumps[MD_PACK_MAX_LUMPS];
static unsigned s_count;
static unsigned s_missing;
static uint32_t s_total;
static uint32_t s_crc;
static uint16_t s_pack_numlumps;
static uint32_t s_pack_dirofs;
static const uint8_t *s_base;

static char s_wad_path[80];
static uint16_t s_wad_numlumps;
static uint32_t s_wad_dirofs;
static bool s_wad_ok;
static FIL s_wad;
static bool s_wad_open;

volatile uint32_t md_pack_loads;
volatile uint32_t md_pack_evicts;
volatile uint32_t md_pack_fails;

static inline uint16_t rd16(const uint8_t *p) {
  return (uint16_t)(p[0] | (p[1] << 8));
}
static inline uint32_t rd32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}
static inline void wr16(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
}
static inline void wr32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16);
  p[3] = (uint8_t)(v >> 24);
}

static uint32_t bitset_crc(const uint16_t *bitset, unsigned numlumps) {
  uint32_t crc = 0xFFFFFFFFu;
  const unsigned words = (numlumps + 15u) >> 4;
  for (unsigned w = 0; w < words; w++) {
    for (unsigned b = 0; b < 2u; b++) {
      crc ^= (uint8_t)(bitset[w] >> (8u * b));
      for (unsigned k = 0; k < 8u; k++) {
        crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
      }
    }
  }
  return ~crc ^ numlumps;
}

static inline bool bit_set(const uint16_t *bitset, unsigned i) {
  return ((bitset[i >> 4] >> (i & 15u)) & 1u) != 0u;
}

static inline uint32_t flash_offset(uint32_t pack_offset) {
  return (uint32_t)((uintptr_t)&_pack_flash_start - XIP_BASE) + pack_offset;
}

static void flash_program(uint32_t off, const uint8_t *data, uint32_t len) {
  md_core1_park();
  const uint32_t ints = save_and_disable_interrupts();
  flash_range_program(off, data, len);
  restore_interrupts(ints);
  md_core1_unpark();
}

static void flash_erase(uint32_t off, uint32_t len) {
  md_core1_park();
  const uint32_t ints = save_and_disable_interrupts();
  flash_range_erase(off, len);
  restore_interrupts(ints);
  md_core1_unpark();
}

static bool wad_open(void) {
  if (s_wad_open) return true;
  if (!s_wad_ok || f_open(&s_wad, s_wad_path, FA_READ) != FR_OK) return false;
  s_wad_open = true;
  return true;
}

static void wad_close(void) {
  if (s_wad_open) f_close(&s_wad);
  s_wad_open = false;
}

static bool wad_read(uint32_t pos, void *dst, uint32_t len) {
  UINT br = 0;
  return f_lseek(&s_wad, pos) == FR_OK &&
         f_read(&s_wad, dst, len, &br) == FR_OK && br == len;
}

/* ------------------------------------------------------------------ */
/* Demand loading                                                       */
/* ------------------------------------------------------------------ */
/*
 * The preloaded pack fills the front of PACK_FLASH; the rest is a ring
 * for lumps the renderer asks for that the pack does not hold -- a level
 * can ask for more than fits (the first shareware level wants 1.6 MB),
 * and much of it is seldom drawn. A miss reads the lump from the WAD on
 * the SD card and programs it into erased ring space, a page at a time.
 *
 * Making room means erasing a sector, which drops the lumps in it. That
 * only happens in md_pack_service(), between frames, so every pointer
 * the renderer got during a frame stays good to the end of it; a miss
 * that would need an erase is queued for then, and the lump is left out
 * of that one frame. Pointers kept longer (colour maps) must come from
 * md_pack_lump_fixed(), i.e. the preloaded part.
 */

typedef struct {
  uint16_t lump; /* 0 = free slot (lump 0 is the WALLSTRT marker) */
  uint16_t page; /* start, in pages from s_base */
  uint32_t size;
} ring_slot_t;

static ring_slot_t s_ring[MD_RING_SLOTS];
static unsigned s_ring_used; /* slots [0, s_ring_used) may be in use */
static uint32_t s_ring_start;
static uint32_t s_ring_end;
static uint32_t s_wp;         /* next write, page aligned */
static uint32_t s_erased_end; /* [s_wp, s_erased_end) erased and free */
static bool s_ring_ok;

static uint16_t s_defer[MD_DEFER_MAX];
static unsigned s_defer_count;
static unsigned s_inline_loads;
static uint16_t s_failed[MD_PACK_MAX_WAD_LUMPS / 16];
static uint8_t s_page[FLASH_PAGE_SIZE];

static int s_memo_lump = -1;
static const uint8_t *s_memo_ptr;
static uint32_t s_memo_size;

static void ring_clear(void) {
  memset(s_ring, 0, sizeof(s_ring));
  memset(s_failed, 0, sizeof(s_failed));
  s_ring_used = 0;
  s_defer_count = 0;
  s_inline_loads = 0;
  s_memo_lump = -1;
}

/* The ring is everything after the preloaded pack, whole sectors. */
static void ring_setup(bool erased) {
  ring_clear();
  s_ring_start = (s_total + FLASH_SECTOR_SIZE - 1u) &
                 ~(uint32_t)(FLASH_SECTOR_SIZE - 1u);
  s_ring_end = pack_flash_size() & ~(uint32_t)(FLASH_SECTOR_SIZE - 1u);
  s_ring_ok = s_count && s_ring_start < s_ring_end;
  s_wp = s_ring_start;
  s_erased_end = erased ? s_ring_end : s_ring_start;
}

static const ring_slot_t *ring_find(int lump) {
  for (unsigned i = 0; i < s_ring_used; i++) {
    if (s_ring[i].lump == lump) return &s_ring[i];
  }
  return NULL;
}

static int ring_free_slot(void) {
  for (unsigned i = 0; i < MD_RING_SLOTS; i++) {
    if (s_ring[i].lump == 0) return (int)i;
  }
  return -1;
}

static void ring_add(unsigned slot, int lump, uint32_t off, uint32_t size) {
  s_ring[slot].lump = (uint16_t)lump;
  s_ring[slot].page = (uint16_t)(off / FLASH_PAGE_SIZE);
  s_ring[slot].size = size;
  if (slot >= s_ring_used) s_ring_used = slot + 1u;
}

/* Erase the ring sector at s_erased_end, dropping the lumps it touches. */
static void ring_erase_next(void) {
  const uint32_t a = s_erased_end / FLASH_PAGE_SIZE;
  const uint32_t b = (s_erased_end + FLASH_SECTOR_SIZE) / FLASH_PAGE_SIZE;
  for (unsigned i = 0; i < s_ring_used; i++) {
    ring_slot_t *r = &s_ring[i];
    if (!r->lump) continue;
    const uint32_t pages = (r->size + FLASH_PAGE_SIZE - 1u) / FLASH_PAGE_SIZE;
    if (r->page < b && r->page + pages > a) {
      r->lump = 0;
      md_pack_evicts++;
    }
  }
  while (s_ring_used && !s_ring[s_ring_used - 1u].lump) s_ring_used--;
  s_memo_lump = -1;
  flash_erase(flash_offset(s_erased_end), FLASH_SECTOR_SIZE);
  s_erased_end += FLASH_SECTOR_SIZE;
}

/* Make the ring's next sector erased space, wrapping at the end. What
 * lies past s_erased_end stays usable until its sector's turn comes. */
static void ring_advance(void) {
  if (s_erased_end + FLASH_SECTOR_SIZE > s_ring_end) {
    s_wp = s_ring_start;
    s_erased_end = s_ring_start;
  }
  ring_erase_next();
}

/* Room for `need` bytes at s_wp, and a slot to index them; erasing (and
 * so evicting) only if `may_erase`. Returns the slot, or -1. */
static int ring_room(uint32_t need, bool may_erase) {
  int slot = ring_free_slot();
  if (s_wp + need <= s_erased_end && slot >= 0) return slot;
  if (!may_erase) return -1;
  if (s_wp + need > s_ring_end) {
    s_wp = s_ring_start;
    s_erased_end = s_ring_start;
  }
  while (s_wp + need > s_erased_end) ring_erase_next();
  /* Slots run out before flash only with many tiny lumps: keep going
   * round until the oldest have gone. */
  for (unsigned n = 0; slot < 0 && n < (s_ring_end - s_ring_start) /
                                          FLASH_SECTOR_SIZE; n++) {
    ring_advance();
    if (s_wp + need > s_erased_end) continue;
    slot = ring_free_slot();
  }
  return s_wp + need <= s_erased_end ? slot : -1;
}

static inline void mark_failed(int lump) {
  s_failed[lump >> 4] |= (uint16_t)(1u << (lump & 15));
  md_pack_fails++;
}

static void defer(int lump) {
  if (s_defer_count < MD_DEFER_MAX) s_defer[s_defer_count++] = (uint16_t)lump;
}

static bool is_deferred(int lump) {
  for (unsigned i = 0; i < s_defer_count; i++) {
    if (s_defer[i] == lump) return true;
  }
  return false;
}

static const uint8_t *demand(int lump, bool may_erase) {
  uint8_t d[WAD_DIR_ENTRY];

  if (!s_ring_ok || lump <= 0 || lump >= (int)s_wad_numlumps ||
      lump >= MD_PACK_MAX_WAD_LUMPS ||
      (s_failed[lump >> 4] & (1u << (lump & 15)))) {
    return NULL;
  }
  if (!may_erase) {
    if (is_deferred(lump)) return NULL;
    if (s_inline_loads >= MD_INLINE_LOADS_PER_FRAME) {
      defer(lump);
      return NULL;
    }
  }
  if (!wad_open() ||
      !wad_read(s_wad_dirofs + (uint32_t)lump * WAD_DIR_ENTRY, d, sizeof(d))) {
    mark_failed(lump);
    return NULL;
  }
  const uint32_t filepos = rd32(d);
  const uint32_t size = rd32(d + 4);
  const uint32_t need =
      (size + FLASH_PAGE_SIZE - 1u) & ~(uint32_t)(FLASH_PAGE_SIZE - 1u);
  if (!size || need > s_ring_end - s_ring_start) {
    mark_failed(lump);
    return NULL;
  }
  const int slot = ring_room(need, may_erase);
  if (slot < 0) {
    if (may_erase) {
      mark_failed(lump);
    } else {
      defer(lump);
    }
    return NULL;
  }

  /* Page by page, so interrupts are never off for long. */
  const uint32_t off = s_wp;
  uint32_t done = 0;
  bool ok = f_lseek(&s_wad, filepos) == FR_OK;
  while (ok && done < size) {
    uint32_t n = size - done;
    if (n > FLASH_PAGE_SIZE) n = FLASH_PAGE_SIZE;
    UINT br = 0;
    if (f_read(&s_wad, s_page, n, &br) != FR_OK || br != n) {
      ok = false;
      break;
    }
    if (n < FLASH_PAGE_SIZE) memset(s_page + n, 0xFF, FLASH_PAGE_SIZE - n);
    flash_program(flash_offset(off + done), s_page, FLASH_PAGE_SIZE);
    done += FLASH_PAGE_SIZE;
  }
  /* Programmed pages are no longer erased, whatever happened. */
  s_wp = off + done;
  if (!ok) {
    mark_failed(lump);
    return NULL;
  }
  ring_add((unsigned)slot, lump, off, size);
  if (!may_erase) s_inline_loads++;
  md_pack_loads++;
  return s_base + off;
}

void md_pack_service(bool (*busy)(void)) {
  s_inline_loads = 0;
  while (s_defer_count && !(busy && busy())) {
    const int lump = s_defer[--s_defer_count];
    if (!ring_find(lump)) demand(lump, true);
  }
}

/* ------------------------------------------------------------------ */
/* Lookup                                                               */
/* ------------------------------------------------------------------ */

void md_pack_init(void) {
  s_base = (const uint8_t *)&_pack_flash_start;
  s_count = 0;
  s_missing = 0;
  s_total = 0;
  s_crc = 0;
  s_ring_ok = false;
  ring_clear();
  if (memcmp(s_base, MD_PACK_MAGIC, 4) != 0 ||
      rd16(s_base + 4) != MD_PACK_VERSION) {
    return;
  }
  const unsigned n = rd16(s_base + 6);
  const uint32_t total = rd32(s_base + 8);
  if (n > MD_PACK_MAX_LUMPS || total > pack_flash_size() ||
      MD_PACK_HEADER_BYTES + n * MD_PACK_ENTRY_BYTES > total) {
    return;
  }
  for (unsigned i = 0; i < n; i++) {
    const uint8_t *e = s_base + MD_PACK_HEADER_BYTES + i * MD_PACK_ENTRY_BYTES;
    s_lumps[i] = rd16(e);
    if (i > 0 && s_lumps[i] <= s_lumps[i - 1]) return; /* not sorted: junk */
    if (rd32(e + 4) + rd32(e + 8) > total) return;
  }
  s_count = n;
  s_total = total;
  s_missing = rd16(s_base + 14);
  s_pack_numlumps = rd16(s_base + 12);
  s_pack_dirofs = rd32(s_base + 16);
  s_crc = rd32(s_base + 20);
  /* What the ring held before is unknown; md_pack_reuse() erases it. */
  ring_setup(false);
  DPRINTF("pack: %u lumps, %lu bytes, %u on demand\n", s_count,
          (unsigned long)s_total, s_missing);
}

uint16_t md_pack_open_wad(const char *folder, const char *name,
                          uint16_t numlumps, uint32_t size, uint32_t dirofs) {
  FIL f;
  uint8_t hdr[12];
  UINT br = 0;

  wad_close();
  s_wad_ok = false;
  snprintf(s_wad_path, sizeof(s_wad_path), "%s/%s", folder, name);
  if (f_open(&f, s_wad_path, FA_READ) != FR_OK) {
    DPRINTF("pack: cannot open %s\n", s_wad_path);
    return MD_ERR_NO_WAD;
  }
  const uint32_t fsize = (uint32_t)f_size(&f);
  const FRESULT fr = f_read(&f, hdr, sizeof(hdr), &br);
  f_close(&f);
  if (fr != FR_OK || br != sizeof(hdr)) return MD_ERR_NO_WAD;
  if ((memcmp(hdr, "IWAD", 4) != 0 && memcmp(hdr, "PWAD", 4) != 0) ||
      rd32(hdr + 4) != numlumps || rd32(hdr + 8) != dirofs || fsize != size) {
    DPRINTF("pack: %s differs from the ST's (%lu lumps @%lu, %lu bytes)\n",
            s_wad_path, (unsigned long)rd32(hdr + 4),
            (unsigned long)rd32(hdr + 8), (unsigned long)fsize);
    return MD_ERR_WAD_MISMATCH;
  }
  s_wad_numlumps = numlumps;
  s_wad_dirofs = dirofs;
  s_wad_ok = true;

  /* A pack from another WAD is no use. */
  if (s_count && (s_pack_numlumps != numlumps || s_pack_dirofs != dirofs)) {
    s_count = 0;
    s_ring_ok = false;
  }
  DPRINTF("pack: WAD %s OK, %u lumps\n", s_wad_path, numlumps);
  return 0;
}

static int find_entry(int lump) {
  int lo = 0;
  int hi = (int)s_count - 1;
  while (lo <= hi) {
    const int mid = (lo + hi) >> 1;
    const int v = s_lumps[mid];
    if (v == lump) return mid;
    if (v < lump) {
      lo = mid + 1;
    } else {
      hi = mid - 1;
    }
  }
  return -1;
}

const uint8_t *md_pack_lump_fixed(int lump) {
  const int i = find_entry(lump);
  if (i < 0) return NULL;
  return s_base +
         rd32(s_base + MD_PACK_HEADER_BYTES + i * MD_PACK_ENTRY_BYTES + 4);
}

const uint8_t *md_pack_lump(int lump) {
  if (lump == s_memo_lump) return s_memo_ptr;
  const int i = find_entry(lump);
  const uint8_t *p = NULL;
  uint32_t size = 0;
  if (i >= 0) {
    const uint8_t *e = s_base + MD_PACK_HEADER_BYTES + i * MD_PACK_ENTRY_BYTES;
    p = s_base + rd32(e + 4);
    size = rd32(e + 8);
  } else {
    const ring_slot_t *r = ring_find(lump);
    if (!r && demand(lump, false)) r = ring_find(lump);
    if (r) {
      p = s_base + (uint32_t)r->page * FLASH_PAGE_SIZE;
      size = r->size;
    }
  }
  if (p) {
    s_memo_lump = lump;
    s_memo_ptr = p;
    s_memo_size = size;
  }
  return p;
}

uint32_t md_pack_lump_size(int lump) {
  return md_pack_lump(lump) ? s_memo_size : 0;
}

bool md_pack_has_all(const uint16_t *bitset, unsigned numlumps) {
  if (!s_count) return false;
  /* Asked for exactly this before: the pack is as complete as it gets,
   * even if some lumps were left for loading on demand. */
  if (s_crc == bitset_crc(bitset, numlumps)) return true;
  for (unsigned i = 0; i < numlumps; i++) {
    if (bit_set(bitset, i) && find_entry((int)i) < 0) return false;
  }
  return true;
}

void md_pack_reuse(md_pack_progress_t progress) {
  /* The ring may hold anything from the last run: start it afresh. */
  ring_setup(true);
  if (!s_ring_ok) return;
  const uint32_t len = s_ring_end - s_ring_start;
  for (uint32_t o = 0; o < len; o += 65536u) {
    uint32_t n = len - o;
    if (n > 65536u) n = 65536u;
    flash_erase(flash_offset(s_ring_start + o), n);
    if (progress) progress((unsigned)((100ull * (o + n)) / len));
  }
}

unsigned md_pack_count(void) { return s_count; }
unsigned md_pack_missing(void) { return s_missing; }
unsigned md_pack_kb(void) { return (unsigned)((s_total + 1023u) >> 10); }

/* ------------------------------------------------------------------ */
/* Building                                                             */
/* ------------------------------------------------------------------ */

typedef struct {
  uint16_t lump;
  uint16_t rank; /* preload order: lower first */
  uint32_t filepos;
  uint32_t size;
} want_t;

typedef struct {
  uint8_t *stage;
  uint32_t fill;    /* bytes in stage */
  uint32_t flash;   /* flash offset of stage[0] (from XIP_BASE) */
  uint32_t written; /* bytes of the pack produced so far */
  uint32_t total;
  md_pack_progress_t progress;
  bool failed;
} writer_t;

/* Build progress: erasing is the first 40%, writing the rest. */
#define MD_PACK_ERASE_PCT 40u

static void writer_flush(writer_t *w) {
  if (!w->fill) return;
  uint32_t len = (w->fill + FLASH_PAGE_SIZE - 1u) & ~(FLASH_PAGE_SIZE - 1u);
  if (len > w->fill) memset(w->stage + w->fill, 0xFF, len - w->fill);
  flash_program(w->flash, w->stage, len);
  w->flash += len;
  w->fill = 0;
  if (w->progress && w->total) {
    w->progress(MD_PACK_ERASE_PCT +
                (unsigned)(((100u - MD_PACK_ERASE_PCT) * (uint64_t)w->written) /
                           w->total));
  }
}

static void writer_put(writer_t *w, const void *src, uint32_t len) {
  const uint8_t *p = (const uint8_t *)src;
  while (len) {
    uint32_t n = MD_PACK_STAGE - w->fill;
    if (n > len) n = len;
    memcpy(w->stage + w->fill, p, n);
    w->fill += n;
    w->written += n;
    p += n;
    len -= n;
    if (w->fill == MD_PACK_STAGE) writer_flush(w);
  }
}

/* Copy `len` bytes at `filepos` of the WAD straight into the stage. */
static void writer_copy(writer_t *w, uint32_t filepos, uint32_t len) {
  if (f_lseek(&s_wad, filepos) != FR_OK) {
    w->failed = true;
    return;
  }
  while (len && !w->failed) {
    uint32_t n = MD_PACK_STAGE - w->fill;
    if (n > len) n = len;
    UINT br = 0;
    if (f_read(&s_wad, w->stage + w->fill, n, &br) != FR_OK || br != n) {
      w->failed = true;
      return;
    }
    w->fill += n;
    w->written += n;
    len -= n;
    if (w->fill == MD_PACK_STAGE) writer_flush(w);
  }
}

static void writer_pad4(writer_t *w) {
  static const uint8_t zero[4] = {0, 0, 0, 0};
  const uint32_t pad = (4u - (w->written & 3u)) & 3u;
  if (pad) writer_put(w, zero, pad);
}

static int by_rank(const void *a, const void *b) {
  const want_t *x = (const want_t *)a;
  const want_t *y = (const want_t *)b;
  if (x->rank != y->rank) return (int)x->rank - (int)y->rank;
  return (int)x->lump - (int)y->lump;
}

static int by_lump(const void *a, const void *b) {
  return (int)((const want_t *)a)->lump - (int)((const want_t *)b)->lump;
}

static uint16_t rank_of(unsigned lump, const md_pack_plan_t *plan) {
  for (unsigned i = 0; i < plan->must_count; i++) {
    if (plan->must[i] == lump) return 0;
  }
  if (lump >= plan->guns_first && lump < plan->guns_end) return 3;
  if (lump >= plan->shape_first) return 2;
  return 1; /* walls, doors, flats, sky: read for every column */
}

uint16_t md_pack_build(const uint16_t *bitset, unsigned numlumps,
                       const md_pack_plan_t *plan, uint8_t *work,
                       uint32_t work_size, md_pack_progress_t progress) {
  uint16_t err = 0;

  if (!s_wad_ok) return MD_ERR_NO_WAD;
  if (numlumps > s_wad_numlumps) numlumps = s_wad_numlumps;
  if (work_size < MD_PACK_STAGE + 64u * sizeof(want_t)) return MD_ERR_PACK_IO;

  want_t *want = (want_t *)(work + MD_PACK_STAGE);
  unsigned want_max = (work_size - MD_PACK_STAGE) / sizeof(want_t);
  if (want_max > MD_PACK_MAX_LUMPS) want_max = MD_PACK_MAX_LUMPS;

  if (!wad_open()) return MD_ERR_NO_WAD;
  if (progress) progress(0);

  /* 1. Directory: where each wanted lump lives. Read in stage-sized
   *    slices; lumps with no data (markers) are skipped. */
  unsigned n = 0;
  unsigned missing = 0;
  const unsigned per_slice = MD_PACK_STAGE / WAD_DIR_ENTRY;
  for (unsigned first = 0; first < numlumps && !err; first += per_slice) {
    unsigned cnt = numlumps - first;
    if (cnt > per_slice) cnt = per_slice;
    if (!wad_read(s_wad_dirofs + first * WAD_DIR_ENTRY, work,
                  cnt * WAD_DIR_ENTRY)) {
      err = MD_ERR_PACK_IO;
      break;
    }
    for (unsigned i = 0; i < cnt; i++) {
      const unsigned lump = first + i;
      const uint16_t rank = rank_of(lump, plan);
      if (rank != 0 && !bit_set(bitset, lump)) continue;
      const uint8_t *d = work + i * WAD_DIR_ENTRY;
      const uint32_t size = rd32(d + 4);
      if (!size) continue;
      if (n == want_max) {
        missing++;
        continue;
      }
      want[n].lump = (uint16_t)lump;
      want[n].rank = rank;
      want[n].filepos = rd32(d);
      want[n].size = size;
      n++;
    }
  }
  if (err) return err;

  /* 2. Choose. Everything, if it fits beside the ring; otherwise by rank
   *    (colour maps, flats and sky; then what every wall column reads;
   *    then sprites; then weapon frames), skipping what no longer fits.
   *    The rest is loaded when first drawn. */
  const uint32_t capacity = pack_flash_size();
  const uint32_t budget =
      capacity > 2u * MD_PACK_RING_MIN ? capacity - MD_PACK_RING_MIN
                                       : capacity / 2u;
  uint32_t want_bytes = 0;
  for (unsigned i = 0; i < n; i++) want_bytes += (want[i].size + 3u) & ~3u;
  uint32_t total = MD_PACK_HEADER_BYTES + n * MD_PACK_ENTRY_BYTES + want_bytes;
  const unsigned asked = n;
  if (total > budget) {
    qsort(want, n, sizeof(want_t), by_rank);
    unsigned keep = 0;
    total = MD_PACK_HEADER_BYTES;
    for (unsigned i = 0; i < n; i++) {
      const uint32_t cost = MD_PACK_ENTRY_BYTES + ((want[i].size + 3u) & ~3u);
      if (total + cost > budget) continue;
      total += cost;
      want[keep++] = want[i];
    }
    missing += n - keep;
    n = keep;
  }
  qsort(want, n, sizeof(want_t), by_lump);
  DPRINTF("pack: level asks for %u lumps, %lu bytes; preloading %u, %lu "
          "bytes\n",
          asked, (unsigned long)want_bytes, n, (unsigned long)total);
  (void)asked;

  /* 3. Erase the whole window, a 64 KB block at a time so the progress
   *    keeps moving: the pack, and the ring after it. */
  const uint32_t erase_len = capacity & ~(uint32_t)(FLASH_SECTOR_SIZE - 1u);
  s_count = 0; /* the old pack is going */
  s_ring_ok = false;
  for (uint32_t o = 0; o < erase_len; o += 65536u) {
    uint32_t len = erase_len - o;
    if (len > 65536u) len = 65536u;
    flash_erase(flash_offset(o), len);
    if (progress) {
      progress((unsigned)((MD_PACK_ERASE_PCT * (uint64_t)(o + len)) /
                          erase_len));
    }
  }

  /* 4. Header, directory, data. */
  writer_t w = {.stage = work,
                .fill = 0,
                .flash = flash_offset(0),
                .written = 0,
                .total = total,
                .progress = progress,
                .failed = false};
  uint8_t hdr[MD_PACK_HEADER_BYTES];
  memcpy(hdr, MD_PACK_MAGIC, 4);
  wr16(hdr + 4, MD_PACK_VERSION);
  wr16(hdr + 6, (uint16_t)n);
  wr32(hdr + 8, total);
  wr16(hdr + 12, s_wad_numlumps);
  wr16(hdr + 14, (uint16_t)missing);
  wr32(hdr + 16, s_wad_dirofs);
  wr32(hdr + 20, bitset_crc(bitset, numlumps));
  writer_put(&w, hdr, sizeof(hdr));

  /* The directory is written from the want[] table, which lives past the
   * stage, so writing it cannot overwrite it. */
  uint32_t ofs = MD_PACK_HEADER_BYTES + n * MD_PACK_ENTRY_BYTES;
  for (unsigned i = 0; i < n; i++) {
    uint8_t e[MD_PACK_ENTRY_BYTES];
    wr16(e, want[i].lump);
    wr16(e + 2, 0);
    wr32(e + 4, ofs);
    wr32(e + 8, want[i].size);
    writer_put(&w, e, sizeof(e));
    ofs += (want[i].size + 3u) & ~3u;
  }
  for (unsigned i = 0; i < n && !w.failed; i++) {
    writer_copy(&w, want[i].filepos, want[i].size);
    writer_pad4(&w);
  }
  if (!w.failed) writer_flush(&w);
  if (w.failed) {
    /* Make sure the half-written pack is not mistaken for a good one. */
    flash_erase(flash_offset(0), FLASH_SECTOR_SIZE);
    md_pack_init();
    return (uint16_t)(err | MD_ERR_PACK_IO);
  }

  md_pack_init();
  ring_setup(true); /* erased above */
  if (progress) progress(100);
  DPRINTF("pack: built %u lumps, %lu bytes, %u on demand, ring %lu KB\n", n,
          (unsigned long)total, missing,
          (unsigned long)((s_ring_end - s_ring_start) >> 10));
  return err;
}
