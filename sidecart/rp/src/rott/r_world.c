/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * File: r_world.c
 * Description: The MD's render-only mirror of the ST's world: tilemap,
 *              the plane 2 values the caster reads, doors, masked walls,
 *              pushwalls, animated walls and light sources, kept current by
 *              the records in WORLD and FRAME commands (rott_md_protocol.h).
 */

#include <string.h>

#include "r_local.h"

word tilemap[MAPSIZE][MAPSIZE];
uint16_t spotvis_bits[MD_BITSET_WORDS];
r_door_t doorobjlist[MAXDOORS];
int doornum;
r_mwall_t maskobjlist[MAXMASKED];
int maskednum;
r_pwall_t pwallobjlist[MAXPWALLS];
int pwallnum;
int animwalls[MAXANIMWALLS];
uint16_t *r_mapseen_bits;
uint16_t r_frame_bits[MD_BITSET_WORDS];

/* Sparse maps keyed by tile index ((x << 7) | y): plane 2 for 0x2000
 * tiles, and light source values. Open addressing, linear probing; a
 * level has a few hundred of each at most. */
#define SPARSE_BITS 10
#define SPARSE_SIZE (1u << SPARSE_BITS)
#define SPARSE_EMPTY 0xFFFFu

typedef struct {
  uint16_t key[SPARSE_SIZE];
  uint16_t value[SPARSE_SIZE];
  unsigned used;
} sparse16_t;

typedef struct {
  uint16_t key[SPARSE_SIZE];
  uint32_t value[SPARSE_SIZE];
  unsigned used;
} sparse32_t;

static sparse16_t s_plane2;
static sparse32_t s_lights;

static inline unsigned sparse_hash(unsigned key) {
  return (key * 2654435761u) >> (32 - SPARSE_BITS);
}

/* Returns the slot holding `key`, or the empty slot where it would go, or
 * -1 when the table is full and the key absent. Values are never removed,
 * only zeroed, so probe chains stay intact. */
static int sparse_slot(const uint16_t *keys, unsigned key) {
  unsigned i = sparse_hash(key);
  for (unsigned n = 0; n < SPARSE_SIZE; n++, i = (i + 1u) & (SPARSE_SIZE - 1u)) {
    if (keys[i] == key || keys[i] == SPARSE_EMPTY) return (int)i;
  }
  return -1;
}

static void sparse16_set(sparse16_t *s, unsigned key, uint16_t value) {
  const int i = sparse_slot(s->key, key);
  if (i < 0) return;
  if (s->key[i] == SPARSE_EMPTY) {
    if (!value || s->used >= SPARSE_SIZE - 1u) return;
    s->key[i] = (uint16_t)key;
    s->used++;
  }
  s->value[i] = value;
}

static void sparse32_set(sparse32_t *s, unsigned key, uint32_t value) {
  const int i = sparse_slot(s->key, key);
  if (i < 0) return;
  if (s->key[i] == SPARSE_EMPTY) {
    if (!value || s->used >= SPARSE_SIZE - 1u) return;
    s->key[i] = (uint16_t)key;
    s->used++;
  }
  s->value[i] = value;
}

int R_Plane2(int x, int y) {
  const int i = sparse_slot(s_plane2.key, ((unsigned)x << 7) | (unsigned)y);
  return (i < 0 || s_plane2.key[i] == SPARSE_EMPTY) ? 0 : s_plane2.value[i];
}

uint32_t R_LightAt(int x, int y) {
  const int i = sparse_slot(s_lights.key, ((unsigned)x << 7) | (unsigned)y);
  return (i < 0 || s_lights.key[i] == SPARSE_EMPTY) ? 0 : s_lights.value[i];
}

void R_WorldReset(void) {
  memset(tilemap, 0, sizeof(tilemap));
  memset(doorobjlist, 0, sizeof(doorobjlist));
  memset(maskobjlist, 0, sizeof(maskobjlist));
  memset(pwallobjlist, 0, sizeof(pwallobjlist));
  memset(animwalls, 0, sizeof(animwalls));
  doornum = 0;
  maskednum = 0;
  pwallnum = 0;
  memset(s_plane2.key, 0xFF, sizeof(s_plane2.key));
  s_plane2.used = 0;
  memset(s_lights.key, 0xFF, sizeof(s_lights.key));
  s_lights.used = 0;
  if (r_mapseen_bits) memset(r_mapseen_bits, 0, MD_BITSET_BYTES);
}

void R_MarkSeen(int x, int y) {
  const uint16_t bit = (uint16_t)(1u << MD_BITSET_BIT(y));
  r_frame_bits[MD_BITSET_WORD(x, y)] |= bit;
  if (r_mapseen_bits) r_mapseen_bits[MD_BITSET_WORD(x, y)] |= bit;
}

/* The frame's published bitset: every tile the caster crossed (spotvis)
 * plus the walls it hit (R_MarkSeen). */
void R_FinishFrameBits(void) {
  for (unsigned w = 0; w < MD_BITSET_WORDS; w++) r_frame_bits[w] |= spotvis_bits[w];
}

void R_ClearFrameBits(void) {
  memset(spotvis_bits, 0, sizeof(spotvis_bits));
  memset(r_frame_bits, 0, sizeof(r_frame_bits));
}

/* CRC-16/CCITT over the tilemap as the ST holds it, to spot desyncs. */
uint16_t R_TilemapCrc(void) {
  uint16_t crc = 0xFFFF;
  const word *t = &tilemap[0][0];
  for (unsigned i = 0; i < MAPSIZE * MAPSIZE; i++) {
    crc ^= t[i];
    for (unsigned k = 0; k < 16u; k++) {
      crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u)
                            : (uint16_t)(crc << 1);
    }
  }
  return crc;
}

static bool apply_record(unsigned type, unsigned count, const uint16_t *it) {
  unsigned i;
  switch (type) {
    case MD_REC_TILE:
      for (i = 0; i < count; i++, it += MD_TILE_WORDS) {
        const unsigned x = it[0] & 0x7Fu;
        const unsigned y = (it[0] >> 8) & 0x7Fu;
        tilemap[x][y] = it[1];
        sparse16_set(&s_plane2, (x << 7) | y, it[2]);
      }
      return true;

    case MD_REC_DOOR:
      for (i = 0; i < count; i++, it += MD_DOOR_WORDS) {
        if (it[0] >= MAXDOORS) return false;
        r_door_t *d = &doorobjlist[it[0]];
        d->tilex = (byte)(it[1] & 0x7Fu);
        d->tiley = (byte)((it[1] >> 8) & 0x7Fu);
        d->vertical = (it[2] & MD_DOOR_VERTICAL) != 0;
        d->flags = (byte)(it[2] >> 8);
        d->texture = it[3];
        d->alttexture = it[4];
        d->sidepic = it[5];
        d->basetexture = it[6];
        d->position = it[7];
        d->action = (byte)it[8];
      }
      return true;

    case MD_REC_MWALL:
      for (i = 0; i < count; i++, it += MD_MWALL_WORDS) {
        if (it[0] >= MAXMASKED) return false;
        r_mwall_t *m = &maskobjlist[it[0]];
        m->tilex = (byte)(it[1] & 0x7Fu);
        m->tiley = (byte)((it[1] >> 8) & 0x7Fu);
        m->flags = it[2];
        m->vertical = (it[3] & MD_MW_VERTICAL) != 0;
        m->active = (it[3] & MD_MW_ACTIVE) != 0;
        m->toptexture = (int16_t)it[4];
        m->midtexture = (int16_t)it[5];
        m->bottomtexture = (int16_t)it[6];
        m->sidepic = it[7];
      }
      return true;

    case MD_REC_PWALL:
      for (i = 0; i < count; i++, it += MD_PWALL_WORDS) {
        if (it[0] >= MAXPWALLS) return false;
        r_pwall_t *p = &pwallobjlist[it[0]];
        p->x = (int32_t)md_get32(it + 1);
        p->y = (int32_t)md_get32(it + 3);
        p->texture = it[5];
        p->action = (byte)it[6];
      }
      return true;

    case MD_REC_ANIM:
      for (i = 0; i < count; i++, it += MD_ANIM_WORDS) {
        if (it[0] >= MAXANIMWALLS) return false;
        animwalls[it[0]] = it[1];
      }
      return true;

    case MD_REC_LIGHT:
      for (i = 0; i < count; i++, it += MD_LIGHT_WORDS) {
        const unsigned x = it[0] & 0x7Fu;
        const unsigned y = (it[0] >> 8) & 0x7Fu;
        sparse32_set(&s_lights, (x << 7) | y, md_get32(it + 1));
      }
      return true;

    case MD_REC_COUNTS:
      if (count < 1) return false;
      doornum = it[0] > MAXDOORS ? MAXDOORS : it[0];
      maskednum = it[1] > MAXMASKED ? MAXMASKED : it[1];
      pwallnum = it[2] > MAXPWALLS ? MAXPWALLS : it[2];
      return true;

    default:
      return R_FrameRecord(type, count, it);
  }
}

static unsigned item_words(unsigned type) {
  switch (type) {
    case MD_REC_TILE: return MD_TILE_WORDS;
    case MD_REC_DOOR: return MD_DOOR_WORDS;
    case MD_REC_MWALL: return MD_MWALL_WORDS;
    case MD_REC_PWALL: return MD_PWALL_WORDS;
    case MD_REC_ANIM: return MD_ANIM_WORDS;
    case MD_REC_LIGHT: return MD_LIGHT_WORDS;
    case MD_REC_COUNTS: return MD_COUNTS_WORDS;
    case MD_REC_MSGS: return 1; /* count is in words */
    case MD_REC_VIEW: return MD_VIEW_WORDS;
    case MD_REC_OBJS: return MD_OBJ_WORDS;
    case MD_REC_WEAPON: return MD_WEAPON_WORDS;
    case MD_REC_OVERLAY: return MD_OVERLAY_WORDS;
    default: return 0;
  }
}

bool R_ApplyRecords(const uint16_t *w, uint32_t nwords) {
  uint32_t i = 0;
  while (i + 2u <= nwords) {
    const unsigned type = w[i];
    const unsigned count = w[i + 1];
    if (type == MD_REC_END) return true;
    const unsigned size = item_words(type);
    if (!size) return false;
    const uint32_t need = (uint32_t)size * count;
    if (i + 2u + need > nwords) return false;
    if (!apply_record(type, count, w + i + 2)) return false;
    i += 2u + need;
  }
  return true;
}
