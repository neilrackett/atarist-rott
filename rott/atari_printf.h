/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * atari_printf.h - the native build's printf, forced on every C file by the
 * Makefile (-include): TOS's console needs \r\n at the end of a line, and
 * libcmini writes a bare \n, so a line after one started where the last
 * ended (MiNTLib, in the SDL builds, adds the \r itself).
 */
#ifndef ATARI_PRINTF_H
#define ATARI_PRINTF_H

#include <stdio.h>

int atari_printf(const char *fmt, ...) __attribute__((format(__printf__, 1, 2)));
/* Calls only: format(printf, ...) attributes keep their meaning. */
#define printf(...) atari_printf(__VA_ARGS__)

#endif
