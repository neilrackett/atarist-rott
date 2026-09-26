/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * File: md_core1.c
 * Description: Core 1 job dispatcher. See md_core1.h. Taken from md-doom's
 *              fb_chunked.c without the framebuffer it lived next to.
 */

#include "md_core1.h"

#include "hardware/sync.h"
#include "pico/multicore.h"

static void __not_in_flash_func(md_core1_loop)(void) {
  for (;;) {
    md_core1_job_t job =
        (md_core1_job_t)(uintptr_t)multicore_fifo_pop_blocking();
    void *arg = (void *)(uintptr_t)multicore_fifo_pop_blocking();
    job(arg);
    multicore_fifo_push_blocking(0); /* done */
  }
}

void md_core1_init(void) { multicore_launch_core1(md_core1_loop); }

void __not_in_flash_func(md_core1_dispatch)(md_core1_job_t job, void *arg) {
  multicore_fifo_push_blocking((uint32_t)(uintptr_t)job);
  multicore_fifo_push_blocking((uint32_t)(uintptr_t)arg);
}

void __not_in_flash_func(md_core1_wait)(void) {
  (void)multicore_fifo_pop_blocking();
}

static volatile uint32_t s_parked, s_park_ack;

static void __not_in_flash_func(md_core1_park_job)(void *arg) {
  volatile uint32_t *flag = (volatile uint32_t *)arg;
  const uint32_t ints = save_and_disable_interrupts();
  s_park_ack = 1;
  while (*flag) tight_loop_contents();
  restore_interrupts(ints);
}

void md_core1_park(void) {
  s_parked = 1;
  s_park_ack = 0;
  md_core1_dispatch(md_core1_park_job, (void *)&s_parked);
  /* Only return once Core 1 is really in the job with interrupts off. */
  while (!s_park_ack) tight_loop_contents();
}

void md_core1_unpark(void) {
  s_parked = 0;
  md_core1_wait();
}
