/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * EmuMD stand-in for rp/src/include/constants.h: just the level pack's
 * window in flash (memmap_rp.ld's PACK_FLASH), in EmuMD's emulated
 * flash.
 */
#ifndef ROTT_EMU_CONSTANTS_H
#define ROTT_EMU_CONSTANTS_H
#include "hardware/regs/addressmap.h"
#include "pico.h"

#define ROTT_PACK_FLASH_OFFSET 0x40000u
#define ROTT_PACK_FLASH_BYTES (896u * 1024u)
#define _pack_flash_start (mdfw_flash[ROTT_PACK_FLASH_OFFSET])
static inline uint32_t pack_flash_size(void) { return ROTT_PACK_FLASH_BYTES; }
#endif
