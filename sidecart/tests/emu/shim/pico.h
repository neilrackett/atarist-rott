/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Host stand-ins for the Pico SDK, so the MD/ROTT firmware's portable
 * parts (md_main.c, md_proto.c, md_pack.c, md_video.c, rott/) build
 * unchanged for the emulator in tests/emu.
 */
#ifndef EMU_PICO_H
#define EMU_PICO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define __not_in_flash_func(f) f
#define __attribute_used__
#define tight_loop_contents() ((void)0)

static inline void __dmb(void) { __sync_synchronize(); }
static inline uint32_t save_and_disable_interrupts(void) { return 0; }
static inline void restore_interrupts(uint32_t s) { (void)s; }

/* Microseconds, from the emulator's clock (mdemu.c). */
uint32_t time_us_32(void);
typedef struct {
  volatile uint32_t timerawl;
} emu_timer_hw_t;
extern emu_timer_hw_t *timer_hw;

typedef void (*irq_handler_t)(void);

#endif
