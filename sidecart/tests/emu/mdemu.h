/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * mdemu.h - the MD/ROTT firmware as a library an Atari emulator can drive.
 *
 * The firmware's portable parts (md_main.c, md_proto.c, md_pack.c,
 * md_video.c and the renderer) compiled for the host, with the Pico SDK,
 * FatFs and flash replaced by shims. The emulator routes reads of the
 * cartridge port here: ROM4 ($FA0000-$FAFFFF) returns the firmware's
 * window, ROM3 ($FB0000-$FBFFFF) feeds the command decoder. Commands run
 * to completion the moment their last word arrives, so to the emulated ST
 * the Multi-device is infinitely fast. The firmware's idle loop runs on
 * every 256th ROM4 read.
 */
#ifndef MDEMU_H
#define MDEMU_H

#include <stdint.h>

/* `sd_root` is a host directory standing in for the SD card; the firmware
 * looks for its WAD in <sd_root>/rott. Returns 0 on success. */
int mdemu_init(const char *sd_root);

/* A 16-bit read of ROM4 at `offset` (0..0xFFFE, even). */
uint16_t mdemu_rom4_word(uint32_t offset);

/* A read of ROM3; `addr16` is the low 16 bits of the address. */
void mdemu_rom3_read(uint32_t addr16);

/* The emulator's clock in microseconds, for the protocol's timeouts. */
void mdemu_set_time_us(uint32_t us);

/* Write the rendered view of every Nth frame to <dir>/mdNNNNN.ppm. */
void mdemu_dump_frames(const char *dir, int every);

#endif
