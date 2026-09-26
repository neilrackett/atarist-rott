/* Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* Host stand-in for the firmware's debug.h. */
#ifndef EMU_DEBUG_H
#define EMU_DEBUG_H
#include "pico.h"
extern int emu_verbose;
#define DPRINTF(fmt, ...)                                   \
  do {                                                      \
    if (emu_verbose) printf("mdemu: " fmt, ##__VA_ARGS__);  \
  } while (0)
#define DPRINTFRAW(fmt, ...) DPRINTF(fmt, ##__VA_ARGS__)
#endif
