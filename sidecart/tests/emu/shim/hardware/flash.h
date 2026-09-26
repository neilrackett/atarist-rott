/* Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* Host stand-in: flash is a RAM array (emu_flash.c). */
#ifndef EMU_HARDWARE_FLASH_H
#define EMU_HARDWARE_FLASH_H
#include "pico.h"
#define FLASH_PAGE_SIZE 256u
#define FLASH_SECTOR_SIZE 4096u
void flash_range_erase(uint32_t offset, size_t count);
void flash_range_program(uint32_t offset, const uint8_t *data, size_t count);
#endif
