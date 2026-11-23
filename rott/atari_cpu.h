/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
/*
 * atari_cpu.h - what the native build's CPU can do, for code that takes a
 * quicker way on a 68020 or later (a TT, a Falcon) in the same program
 * (atari_cpu.S: FixedMul, FixedScale; libgcc's multiplies and divides
 * are STDL's libstdl-arith020.o).
 */
#ifndef ATARI_CPU_H
#define ATARI_CPU_H

#if defined(ATARI_NATIVE)
extern unsigned char atari_cpu_l32; /* a 68020 or later */
extern unsigned char atari_cpu_l64; /* a 68020 to 68040 */
/* From the _CPU cookie; first thing in main. */
void atari_cpu_init(void);
#endif

#endif
