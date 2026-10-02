/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * File: rott_md_protocol.h
 * Description: MD/ROTT wire protocol, shared by the ST client (rott/) and
 *              the Multi-device firmware (sidecart/rp/). The single source
 *              of truth for the ROM4 layout, the command ids and every
 *              packet layout -- neither side hard-codes any of these.
 *
 * GPL-2.0-or-later (not GPL-3) because rott/ includes it.
 *
 * Transport recap
 * ---------------
 * ST -> MD: the SidecarTridge sync command protocol (sidecart_stubs.S). The
 * ST reads ROM3 at $FB8000 + (signed) word, one 16-bit word per read, framed
 * as magic $ABCD, command, size, random token, payload, checksum. The MD
 * decodes it in an interrupt and writes the token back to MD_TOKEN_OFFSET to
 * release the ST, so a command costs the upload time and nothing more.
 *
 * MD -> ST: the ST reads the 64 KB ROM4 window at $FA0000, served by the RP
 * straight from RAM.
 *
 * Wire order
 * ----------
 * Everything is a sequence of 16-bit WORDS whose VALUE survives the trip in
 * both directions (the cartridge bus is 16 bits wide). So:
 *
 *   - 16-bit fields are plain words.
 *   - 32-bit fields are two words, LOW WORD FIRST (the RP's native order),
 *     written and read with md_put32() / md_get32(), which work on value
 *     level and so behave the same on the big-endian ST and the
 *     little-endian RP.
 *   - Byte strings (palette RGB, text) travel as the ST holds them in
 *     memory: each word carries bytes 2i and 2i+1 as (high, low). The RP
 *     unpacks them with md_get_bytes(). Nothing else carries bytes.
 *
 * In ROM4 (MD -> ST) every field is a 16-bit word for the same reason; no
 * 32-bit values are published there.
 */

#ifndef ROTT_MD_PROTOCOL_H
#define ROTT_MD_PROTOCOL_H

#include <stdint.h>

#define MD_PROTOCOL_VERSION 2

/* ------------------------------------------------------------------ */
/* Bus windows                                                          */
/* ------------------------------------------------------------------ */

#define MD_ROM4_BASE 0xFA0000UL /* 64 KB, MD -> ST                     */
#define MD_ROM3_BASE 0xFB0000UL /* command channel, ST -> MD           */

/* ------------------------------------------------------------------ */
/* ROM4 layout (offsets from MD_ROM4_BASE)                              */
/* ------------------------------------------------------------------ */

/* Cartridge header + boot stub (target/atarist/src/main.s). No autostart
 * beyond printing a one-line banner at boot. */
#define MD_HEADER_OFFSET 0x0000
#define MD_HEADER_SIZE 0x0400

/* Status block: MD_STATUS_WORDS 16-bit words, see MD_ST_* below. */
#define MD_STATUS_OFFSET 0x0400
#define MD_STATUS_WORDS 32

/* The 16 ST palette words the MD derived from the last PALETTE command.
 * The ST derives the same 16 itself (identical median cut); these are
 * published so the two can be compared. */
#define MD_PALETTE_OFFSET 0x0440
#define MD_PALETTE_WORDS 16

/* 128 x 128 tile bitsets, one bit per tile, as 1024 words: tile (x, y) is
 * word ((x << 7) | y) >> 4, bit (y & 15). Bit order within the word is
 * value order, so the ST tests (word >> (y & 15)) & 1.
 *   SPOTVIS: tiles next to one the ray caster touched for the READY frame
 *            (a floor tile it crossed or a wall it hit, or one of their
 *            eight neighbours), used by the ST to choose which objects to
 *            send: one bit per object rather than nine.
 *   MAPSEEN: everything seen since LEVEL_BEGIN, ORed into mapseen[] by the
 *            ST when the automap opens. */
#define MD_SPOTVIS_OFFSET 0x0460
#define MD_MAPSEEN_OFFSET 0x0C60
#define MD_BITSET_WORDS 1024
#define MD_BITSET_BYTES 2048
#define MD_BITSET_WORD(x, y) ((((unsigned)(x) << 7) | (unsigned)(y)) >> 4)
#define MD_BITSET_BIT(y) ((unsigned)(y) & 15u)

/* Two frame buffers, each up to MD_VIEW_MAX_W x MD_VIEW_MAX_H 4-plane ST
 * low-res pixels. A frame is stored row by row, natural order, each row
 * (w / 2) bytes of interleaved plane words exactly as they go on screen,
 * so the ST copies row r to screen line (y + r) at byte (x / 2). The MD
 * renders into the buffer that does not hold the latest published frame. */
#define MD_VIEW_MAX_W 320
#define MD_VIEW_MAX_H 168
#define MD_FRAME_BYTES (MD_VIEW_MAX_W / 2 * MD_VIEW_MAX_H) /* 26,880 */
#define MD_FRAME_OFFSET_A 0x1480
#define MD_FRAME_OFFSET_B (MD_FRAME_OFFSET_A + MD_FRAME_BYTES) /* $7D80 */
#define MD_FRAME_END (MD_FRAME_OFFSET_B + MD_FRAME_BYTES)      /* $E680 */

/* Protocol block. The first three addresses are fixed by
 * sidecart_stubs.S (and by stdoom/md-js, which share it). */
#define MD_TOKEN_OFFSET 0xF000 /* RP writes the ST's token here         */
#define MD_SEED_OFFSET 0xF004  /* next token seed                       */
#define MD_READY_OFFSET 0xF00A /* both bytes = MD_READY_MAGIC when up    */
#define MD_READY_MAGIC 0x52    /* 'R'                                    */
/* Result text: version string after HELLO, error text after a failure.
 * Stored as ST-order bytes (read with move.b from the ST). */
#define MD_RESULT_OFFSET 0xF100
#define MD_RESULT_SIZE 256

#if (MD_FRAME_END > MD_TOKEN_OFFSET)
#error "frame buffers overlap the protocol block"
#endif

/* ------------------------------------------------------------------ */
/* Status block words (index into the MD_STATUS_WORDS array)            */
/* ------------------------------------------------------------------ */

#define MD_ST_MAGIC 0        /* MD_STATUS_MAGIC once the firmware is up */
#define MD_ST_PROTO 1        /* MD_PROTOCOL_VERSION                     */
#define MD_ST_HEARTBEAT 2    /* incremented by the MD main loop          */
#define MD_ST_READY_SEQ 3    /* seq of the newest complete frame         */
#define MD_ST_READY_BUF 4    /* 0 = buffer A, 1 = buffer B               */
#define MD_ST_VIEW_X 5       /* where the ready frame goes on screen     */
#define MD_ST_VIEW_Y 6
#define MD_ST_VIEW_W 7
#define MD_ST_VIEW_H 8
#define MD_ST_LEVEL_SEQ 9    /* LEVEL_BEGIN serial the world belongs to  */
#define MD_ST_LEVEL_STATE 10 /* MD_LEVEL_*                               */
#define MD_ST_PROGRESS 11    /* 0..100 while a level pack is built       */
#define MD_ST_TILE_CRC 12    /* CRC-16 of the tilemap mirror at LEVEL_END*/
#define MD_ST_ERRORS 13      /* sticky MD_ERR_* bits                      */
#define MD_ST_CMDS 14        /* commands accepted (low 16 bits)          */
#define MD_ST_CHKERRS 15     /* checksum errors seen by the decoder      */
#define MD_ST_DROPS 16       /* commands dropped because the queue was full */
#define MD_ST_RENDER_US 17   /* render time of the ready frame, us        */
#define MD_ST_C2P_US 18      /* dither + c2p time of the ready frame, us  */
#define MD_ST_PAL_SEQ 19     /* PALETTE serial in use                     */
#define MD_ST_PACK_LUMPS 20  /* lumps in the current pack                 */
#define MD_ST_PACK_MISSING 21/* lumps asked for but left to load on demand */
#define MD_ST_PACK_KB 22     /* pack size in KB                            */
#define MD_ST_ECHO_OK 23     /* ECHO commands whose payload checked out    */
#define MD_ST_ECHO_BAD 24    /* ECHO commands whose payload did not        */
#define MD_ST_FRAMES 25      /* frames rendered (low 16 bits)              */
#define MD_ST_LAST_CMD 26    /* id of the last command dispatched          */
#define MD_ST_LOADS 27       /* lumps loaded from SD on demand (low 16)   */
#define MD_ST_EVICTS 28      /* demand-loaded lumps dropped for room       */
#define MD_ST_LOAD_FAILS 29  /* lumps that could not be loaded at all      */

#define MD_STATUS_MAGIC 0x4D52 /* 'MR' */

#define MD_LEVEL_NONE 0    /* no level; FRAMEs are ignored                */
#define MD_LEVEL_LOADING 1 /* building the level pack from the SD card    */
#define MD_LEVEL_PACKED 2  /* pack ready; waiting for TILEMAP/WORLD/END  */
#define MD_LEVEL_READY 3   /* LEVEL_END received; FRAMEs are rendered    */
#define MD_LEVEL_ERROR 4   /* pack failed; see MD_ST_ERRORS and result   */

#define MD_ERR_NO_SD 0x0001      /* SD card did not mount                 */
#define MD_ERR_NO_WAD 0x0002     /* WAD named in HELLO not in the folder  */
#define MD_ERR_WAD_MISMATCH 0x0004 /* numlumps / size differ from the ST's */
#define MD_ERR_PACK_FULL 0x0008  /* a lump could not be loaded at all      */
#define MD_ERR_PACK_IO 0x0010    /* read or flash error building the pack */
#define MD_ERR_BAD_CMD 0x0020    /* unknown command or malformed record   */
#define MD_ERR_NO_LEVEL 0x0040   /* FRAME before LEVEL_END                */
#define MD_ERR_TOO_MANY 0x0080   /* a table index was out of range        */

/* ------------------------------------------------------------------ */
/* Commands                                                             */
/* ------------------------------------------------------------------ */
/* Write commands carry three header longwords (d3, d4, d5 in the stub) and
 * then a buffer of at most MD_CMD_MAX_BYTES. Sync commands carry d3, d4. */

#define MD_CMD_MAX_BYTES 2048

/* HELLO (write). d3 = MD_HELLO_MAGIC, d4 = MD_PROTOCOL_VERSION,
 * d5 = MD_HELLO_* flags. Buffer: md_hello words (below). The MD fills the
 * status block, opens the WAD and writes its version into the result. */
#define MD_CMD_HELLO 0x0040
#define MD_HELLO_MAGIC 0x524F5454UL /* 'ROTT' */
#define MD_HELLO_MSTE 0x0001        /* informational only */
#define MD_HELLO_STE 0x0002
#define MD_HELLO_NOIR 0x0004

/* IDLE (sync). The ST left the 3D view (menus, cinematics). */
#define MD_CMD_IDLE 0x0041

/* LEVEL_BEGIN (write). d3 = level serial, d4 = numlumps covered by the
 * bitset, d5 = 0. Buffer: MD_LV_* words then the lump bitset (bit i of
 * the whole bitset = lump i; word i >> 4, bit i & 15). The MD clears its
 * world, then builds the level pack unless it already holds every lump
 * (MD_ST_LEVEL_STATE goes LOADING, then PACKED or ERROR). The ST waits
 * for PACKED before sending TILEMAP / WORLD / LEVEL_END. */
#define MD_CMD_LEVEL_BEGIN 0x0042

/* TILEMAP (write). d3 = first tile index ((x << 7) | y), d4 = word count,
 * d5 = 0. Buffer: raw tilemap words from the ST's tilemap[x][y]. */
#define MD_CMD_TILEMAP 0x0043

/* WORLD (write). d3 = level serial. Buffer: records (MD_REC_*). Used for
 * the level snapshot and for deltas that did not fit in a FRAME. */
#define MD_CMD_WORLD 0x0044

/* PALETTE (write). d3 = palette serial, d4 = MD_PAL_* flags. Buffer: the
 * 768-byte RGB palette exactly as the ST handed it to its own c2p. */
#define MD_CMD_PALETTE 0x0045
#define MD_PAL_NOIR 0x0001
#define MD_PAL_NOIR_DITHER 0x0002

/* FRAME (write). d3 = frame seq, d4 = seq of the last frame the ST copied,
 * d5 = level serial. Buffer: records, starting with MD_REC_VIEW. */
#define MD_CMD_FRAME 0x0046

/* TEST (write, M1). d3 = frame seq, d4 = pattern, d5 = (w << 16) | h.
 * Buffer: MD_TEST_* words. The MD renders a test pattern and publishes it
 * exactly like a frame. */
#define MD_CMD_TEST 0x0047

/* ECHO (write, M1). d3 = seq, d4 = sum of the buffer's words (mod 2^32),
 * d5 = word count. The MD recomputes the sum and counts ECHO_OK/ECHO_BAD. */
#define MD_CMD_ECHO 0x0048

/* LEVEL_END (sync). d3 = level serial. The snapshot is complete. */
#define MD_CMD_LEVEL_END 0x0049

/* ------------------------------------------------------------------ */
/* HELLO buffer                                                         */
/* ------------------------------------------------------------------ */
#define MD_HELLO_NUMLUMPS 0 /* lumps in the first WAD                   */
#define MD_HELLO_WADSIZE 1  /* 2 words: WAD file size                    */
#define MD_HELLO_DIROFS 3   /* 2 words: WAD directory offset             */
#define MD_HELLO_NAME 5     /* WAD file name, bytes, 0-terminated, 8.3   */
#define MD_HELLO_NAME_WORDS 8
#define MD_HELLO_WORDS (MD_HELLO_NAME + MD_HELLO_NAME_WORDS)

/* ------------------------------------------------------------------ */
/* LEVEL_BEGIN buffer                                                   */
/* ------------------------------------------------------------------ */
#define MD_LV_MAPON 0        /* gamestate.mapon                             */
#define MD_LV_SKY 1          /* 0 = ceiling, 1..6 = sky                     */
#define MD_LV_SKYTOP 2       /* lumps, when sky != 0                        */
#define MD_LV_SKYBOTTOM 3
#define MD_LV_SKYCENTER 4    /* centerskypost                               */
#define MD_LV_FLOOR 5        /* floor lump                                  */
#define MD_LV_CEILING 6      /* ceiling lump, 0 when sky                    */
#define MD_LV_FLAGS 7        /* MD_LVF_*                                    */
#define MD_LV_MAXHEIGHT 8
#define MD_LV_NOMINALHEIGHT 9
#define MD_LV_LEVELHEIGHT 10
#define MD_LV_COLORMAP 11    /* "colormap" lump                             */
#define MD_LV_SPECMAPS 12    /* "specmaps" marker (redmap is +1)            */
#define MD_LV_PLAYMAPS 13    /* "playmaps" marker (player maps from +1)     */
#define MD_LV_WSTART 14      /* WALLSTRT                                    */
#define MD_LV_SHAPESTART 15  /* SHAPSTRT                                    */
#define MD_LV_GUNSSTART 16   /* GUNSTART                                    */
#define MD_LV_ELEVSTART 17   /* elevatorstart                               */
#define MD_LV_FONT 18        /* message font lump                           */
#define MD_LV_DIFFICULTY 19  /* gamestate.difficulty                        */
#define MD_LV_SHAPESTOP 20   /* SHAPSTOP                                    */
#define MD_LV_WORDS 24       /* the bitset follows at this word             */

#define MD_LVF_LIGHTNING 0x0001 /* sky lightning                           */
#define MD_LVF_FOG 0x0002
#define MD_LVF_LIGHTSOURCE 0x0004

/* ------------------------------------------------------------------ */
/* Records (WORLD and FRAME buffers)                                    */
/* ------------------------------------------------------------------ */
/* Each record is [type][count] then count items of the type's fixed size
 * in words. A buffer ends with MD_REC_END or simply runs out. Items hold
 * absolute values, never increments, so a resent record is harmless. */

#define MD_REC_END 0x00

/* Tile: xy = x | (y << 8), tilemap value, plane 2 value (MAPSPOT(x,y,2),
 * read by the caster for 0x2000 tiles). */
#define MD_REC_TILE 0x01
#define MD_TILE_WORDS 3

/* Door: index, xy, flags (MD_DOOR_* | DF_* << 8), texture, alttexture,
 * sidepic, basetexture, position (0..0xffff), action (dr_*). */
#define MD_REC_DOOR 0x02
#define MD_DOOR_WORDS 9
#define MD_DOOR_VERTICAL 0x0001

/* Masked wall: index, xy, flags (MW_*), state (MD_MW_*), top, mid,
 * bottom, sidepic. Textures are signed (-1 = none). */
#define MD_REC_MWALL 0x03
#define MD_MWALL_WORDS 8
#define MD_MW_VERTICAL 0x0001
#define MD_MW_ACTIVE 0x0002 /* on FIRSTMASKEDWALL's list */

/* Pushwall: index, x (2 words), y (2 words), texture, action (pw_*). */
#define MD_REC_PWALL 0x04
#define MD_PWALL_WORDS 7

/* Animated wall: index, texture. */
#define MD_REC_ANIM 0x05
#define MD_ANIM_WORDS 2

/* Light source: xy, value (2 words). Only non-zero tiles are sent; a zero
 * value removes the tile. */
#define MD_REC_LIGHT 0x06
#define MD_LIGHT_WORDS 3

/* Table sizes: doornum, maskednum, pwallnum. */
#define MD_REC_COUNTS 0x07
#define MD_COUNTS_WORDS 3

/* Messages: count = number of words that follow (not items): for each line
 * a word with its byte length, then the text bytes (ST order, padded to a
 * word). count 0 clears the messages. */
#define MD_REC_MSGS 0x08

/* View block, first record of every FRAME. MD_VIEW_WORDS words. */
#define MD_REC_VIEW 0x10
#define MD_V_VIEWX 0      /* 2 words, 16.16                               */
#define MD_V_VIEWY 2      /* 2 words                                       */
#define MD_V_ANGLE 4      /* viewangle 0..2047                             */
#define MD_V_PHEIGHT 5    /* pheight (with bob)                            */
#define MD_V_NONBOB 6     /* nonbobpheight                                 */
#define MD_V_YZANGLE 7    /* 0..2047; the MD derives centery               */
#define MD_V_FOCAL 8      /* focalwidth                                    */
#define MD_V_BOBX 9       /* weaponbobx                                    */
#define MD_V_BOBY 10      /* weaponboby                                    */
#define MD_V_SCREENX 11   /* destination rect on the ST screen             */
#define MD_V_SCREENY 12
#define MD_V_WIDTH 13     /* multiple of 32, <= MD_VIEW_MAX_W              */
#define MD_V_HEIGHT 14    /* <= MD_VIEW_MAX_H                              */
#define MD_V_FLAGS 15     /* MD_VF_*                                       */
#define MD_V_MINSHADE 16
#define MD_V_MAXSHADE 17
#define MD_V_NORMALSHADE 18
#define MD_V_BASEMAXSHADE 19
#define MD_V_LIGHTNINGLVL 20
#define MD_V_GASINDEX 21
#define MD_V_TRANSLEVEL 22 /* transparentlevel                              */
#define MD_V_TICS 23       /* GetTicCount() low 16 bits                     */
#define MD_V_PLAYERZ 24    /* player->z (sky offset)                         */
#define MD_V_BORDER 25     /* SetBorderColor colour, 0 = none                */
#define MD_VIEW_WORDS 26

#define MD_VF_FULLLIGHT 0x0001
#define MD_VF_FOG 0x0002
#define MD_VF_LIGHTNING 0x0004
#define MD_VF_GASON 0x0008
#define MD_VF_SHROOMS 0x0010
#define MD_VF_PAUSED 0x0020
#define MD_VF_LIGHTSOURCE 0x0040

/* Objects (sprites): x >> 8, y >> 8 (1/256 tile), z, shapenum (absolute
 * lump), flags (MD_OF_*), extra (colour / map index). The ST applies
 * rotations and height flips; the MD transforms, lights, sorts, clips and
 * draws, re-checking visibility against its own spotvis. */
#define MD_REC_OBJS 0x11
#define MD_OBJ_WORDS 6
#define MD_OF_KIND_MASK 0x0007
#define MD_OF_NORMAL 0x0000
#define MD_OF_TRANSLUCENT 0x0001 /* extra = translucency level          */
#define MD_OF_SOLID 0x0002       /* extra = colour                      */
#define MD_OF_COLORED 0x0003     /* extra = player colour map 0..10     */
#define MD_OF_REDMAP 0x0004      /* extra = redindex (1..)              */
#define MD_OF_FULLBRIGHT 0x0008
#define MD_OF_EASTWEST 0x0010    /* facing east or west (light intercept) */

/* Weapon: up to two ScaleWeapon calls. Items: xoff, yoff, shapenum,
 * scale mode (-1 = shrink by the bob delta, 0 = none, +1 = grow). */
#define MD_REC_WEAPON 0x12
#define MD_WEAPON_WORDS 4

/* Overlays: flags (MD_OV_*), gasmask lump, eye x, eye y, eye shape,
 * screen-sized sprite lump (NET). */
#define MD_REC_OVERLAY 0x13
#define MD_OVERLAY_WORDS 6
#define MD_OV_GASMASK 0x0001
#define MD_OV_EYE 0x0002
#define MD_OV_NET 0x0004

/* ------------------------------------------------------------------ */
/* TEST buffer (M1)                                                     */
/* ------------------------------------------------------------------ */
#define MD_TEST_SCREENX 0
#define MD_TEST_SCREENY 1
#define MD_TEST_WORDS 2

/* ------------------------------------------------------------------ */
/* Value helpers (both endians)                                         */
/* ------------------------------------------------------------------ */

static inline void md_put32(uint16_t *p, uint32_t v) {
  p[0] = (uint16_t)(v & 0xFFFFu);
  p[1] = (uint16_t)(v >> 16);
}

static inline uint32_t md_get32(const uint16_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 16);
}

/* Unpack n bytes carried ST-style (high byte first in each word). */
static inline void md_get_bytes(uint8_t *dst, const uint16_t *src,
                                unsigned n) {
  unsigned i;
  for (i = 0; i + 1u < n; i += 2u) {
    const uint16_t w = src[i >> 1];
    dst[i] = (uint8_t)(w >> 8);
    dst[i + 1u] = (uint8_t)(w & 0xFFu);
  }
  if (i < n) dst[i] = (uint8_t)(src[i >> 1] >> 8);
}

#endif /* ROTT_MD_PROTOCOL_H */
