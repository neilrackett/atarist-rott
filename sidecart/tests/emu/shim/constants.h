/* Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* Host stand-in for the firmware's constants.h: the flash is a RAM array
 * and XIP_BASE its address, so md_pack.c's offset arithmetic holds. */
#ifndef EMU_CONSTANTS_H
#define EMU_CONSTANTS_H
#include "pico.h"
#define EMU_FLASH_BYTES (2u * 1024u * 1024u)
#define EMU_PACK_OFFSET 0x40000u
#define EMU_PACK_BYTES (896u * 1024u)
extern uint8_t emu_flash[EMU_FLASH_BYTES];
#define XIP_BASE ((uint32_t)(uintptr_t)emu_flash)
#define _pack_flash_start (emu_flash[EMU_PACK_OFFSET])
static inline uint32_t pack_flash_size(void) { return EMU_PACK_BYTES; }
#endif
