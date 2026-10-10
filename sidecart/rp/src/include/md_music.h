/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * File: md_music.h
 * Description: ROTT's music on the Accelerator (MD_CMD_MUSIC). See
 *              md_music.c.
 */

#ifndef MD_MUSIC_H
#define MD_MUSIC_H

#include <stdbool.h>
#include <stdint.h>

/* Once, from Core 0 after Core 1 is up: the registers at MD_YM_OFFSET of
 * the cartridge window, and the 50Hz timer on Core 1. */
void md_music_init(uintptr_t rom_base);

/* An MD_CMD_MUSIC command (MD_MUSIC_*). PLAY reads the song's lump from
 * the WAD, which md_pack_open_wad has opened. */
void md_music_command(unsigned action, int lump, bool loop, unsigned volume);

/* Between commands: the song's windows topped up from the WAD. */
void md_music_service(void);

#endif /* MD_MUSIC_H */
