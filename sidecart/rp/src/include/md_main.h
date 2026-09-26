/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * File: md_main.h
 * Description: MD/ROTT command handling (md_main.c), platform-free.
 */

#ifndef MD_MAIN_H
#define MD_MAIN_H

#include <stdbool.h>
#include <stdint.h>

/* `rom_base` is where the ST's ROM4 window ($FA0000) lives in RP memory;
 * `folder` holds the WAD. Call after md_proto_init(). */
void md_main_init(uintptr_t rom_base, const char *folder, bool sd_ok);

/* Publish the ready magic: the ST may start sending. */
void md_main_ready(void);

/* Run the oldest queued command, if any. Returns true if it ran one. */
bool md_main_poll(void);

#endif /* MD_MAIN_H */
