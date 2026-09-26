/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * File: md_pack_format.h
 * Description: Layout of an MD/ROTT level pack: the lumps one level's
 *              renderer touches, copied byte for byte from the WAD and
 *              keyed by their original lump numbers, so the ST sends lump
 *              numbers unchanged. Built on the Multi-device from the WAD on
 *              its SD card (md_pack.c).
 *
 *   header        MD_PACK_HEADER_BYTES
 *   directory     count x MD_PACK_ENTRY_BYTES, sorted by lump number
 *   data          each lump 4-byte aligned, in directory order
 *
 * Everything little-endian, like the WAD itself. The rest of the flash
 * window, from the next 4 KB boundary, is md_pack.c's ring of lumps
 * loaded on demand; it has no format of its own (it is indexed in RAM
 * and emptied whenever a level starts).
 */

#ifndef MD_PACK_FORMAT_H
#define MD_PACK_FORMAT_H

#define MD_PACK_MAGIC "MDRP"
#define MD_PACK_VERSION 1

/* Header:
 *    0  char[4]  magic "MDRP"
 *    4  u16      version
 *    6  u16      count (lumps in the pack)
 *    8  u32      total bytes (header + directory + data)
 *   12  u16      numlumps of the WAD it came from
 *   14  u16      missing (lumps asked for, left to load on demand)
 *   16  u32      WAD directory offset (identifies the WAD)
 *   20  u32      CRC-32 of the requested lump bitset
 */
#define MD_PACK_HEADER_BYTES 24

/* Directory entry:
 *    0  u16      lump number
 *    2  u16      reserved (0)
 *    4  u32      offset of the data from the start of the pack
 *    8  u32      size in bytes
 */
#define MD_PACK_ENTRY_BYTES 12

#endif /* MD_PACK_FORMAT_H */
