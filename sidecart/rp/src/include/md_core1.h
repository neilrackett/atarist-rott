/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * File: md_core1.h
 * Description: Core 1 job dispatcher, from md-doom's fb_chunked.c.
 *
 * Core 1 waits in a RAM-resident loop for (job, arg) pairs on the
 * inter-core FIFO. md_core1_dispatch() hands it one and returns at once;
 * the caller does its own half of the work and joins with md_core1_wait().
 * Exactly one wait per dispatch. The FIFO push/pop carry the memory
 * barriers, so data prepared before the dispatch is visible to the job and
 * the job's writes are visible after the wait.
 */

#ifndef MD_CORE1_H
#define MD_CORE1_H

#include "pico.h"

typedef void (*md_core1_job_t)(void *arg);

/* Launch Core 1. Once, from Core 0, at boot. */
void md_core1_init(void);

void __not_in_flash_func(md_core1_dispatch)(md_core1_job_t job, void *arg);
void __not_in_flash_func(md_core1_wait)(void);

/* Hold Core 1 in a RAM spin with interrupts masked so Core 0 can erase or
 * program flash (Core 1's idle FIFO pop is flash-resident). Counts as one
 * dispatch/wait pair. */
void md_core1_park(void);
void md_core1_unpark(void);

#endif /* MD_CORE1_H */
