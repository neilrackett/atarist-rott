/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
/*
 * atari_fixmul.h - FixedMul in line, for the renderer's hottest callers
 * (the ray cast, masked walls; native build). On a 68000 (and a 68060)
 * atari_cpu.S's FixedMul sequence in registers, without the call: the
 * three mulu.w of watcom.h's atari_fixed_mul16, (|a| * |b| + 0x8000) >> 16
 * with the sign after; on a 68020 to 68040 FixedMul's own: a mulu.l of 64
 * bits, the same answer. tools/bench/fixtest.c checks it.
 */
#ifndef ATARI_FIXMUL_H
#define ATARI_FIXMUL_H

extern unsigned char atari_cpu_l64; /* atari_cpu.S */
/* FixedMul itself is declared before this (watcom.h) */

static __inline__ long atari_FixedMul(long a, long b)
{
   long r, t, s;

   if (atari_cpu_l64)
   {
      /* mulu.l d1,d1:d0 of the magnitudes (written as words: the game is
       * assembled for the 68000), then the C's rounding and sign */
      register unsigned long lo __asm__("d0") = (unsigned long)(a < 0 ? -a : a);
      register unsigned long hi __asm__("d1") = (unsigned long)(b < 0 ? -b : b);
      unsigned long x;

      __asm__(".word 0x4C01,0x0401" : "+d"(lo), "+d"(hi) : : "cc");
      x = lo + 0x8000UL;
      hi += x < lo;
      x = (hi << 16) | (x >> 16);
      return ((a ^ b) < 0) ? -(long)x : (long)x;
   }
   __asm__(
      "move.l  %1,%3\n\t"
      "eor.l   %2,%3\n\t"            /* the sign */
      "tst.l   %1\n\t"
      "bpl.s   1f\n\t"
      "neg.l   %1\n"
      "1:\ttst.l   %2\n\t"
      "bpl.s   2f\n\t"
      "neg.l   %2\n"
      "2:\tmove.w  %1,%0\n\t"
      "mulu.w  %2,%0\n\t"            /* al * bl */
      "add.l   #0x8000,%0\n\t"
      "clr.w   %0\n\t"
      "swap    %0\n\t"
      "swap    %2\n\t"
      "move.w  %1,%4\n\t"
      "mulu.w  %2,%4\n\t"            /* al * bh */
      "add.l   %4,%0\n\t"
      "swap    %1\n\t"
      "move.w  %1,%4\n\t"
      "mulu.w  %2,%4\n\t"            /* ah * bh */
      "swap    %4\n\t"
      "clr.w   %4\n\t"
      "add.l   %4,%0\n\t"
      "swap    %2\n\t"
      "mulu.w  %2,%1\n\t"            /* ah * bl */
      "add.l   %1,%0\n\t"
      "tst.l   %3\n\t"
      "bpl.s   3f\n\t"
      "neg.l   %0\n"
      "3:"
      : "=&d"(r), "+d"(a), "+d"(b), "=&d"(t), "=&d"(s)
      :
      : "cc");
   return r;
}

#endif
