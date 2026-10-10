/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef MEGASTE_H
#define MEGASTE_H

int is_megaste(void);
void megaste_enable_16mhz_cache(void);
extern int megaste_ctl_boot, megaste_ctl_set;
#if defined(ATARI_NATIVE)
extern unsigned int cpu_speed_passes;
void cpu_speed_measure(void);
#endif

#endif /* MEGASTE_H */
