/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * atari_cpu.S - the 68020's own multiply and divide for the native build's
 * fixed-point arithmetic (FixedMul, FixedScale, atari_mulshift64), chosen
 * at run time, so one 68000 program is quicker on a TT or a Falcon
 * (atari_cpu.h). Each answer is the 68000 path's, to the bit, as the
 * game's logic must give the same on every machine.
 *
 * libgcc's own 32- and 64-bit multiplies and divides (__mulsi3,
 * __udivsi3 and so on) are STDL's libstdl-arith020.o, wrapped at the
 * link (STDL_ARITH020, the Makefile): libgcc's answers to the bit, the
 * 68020's instructions where it has them; on a 68000 its first call
 * writes a jmp to libgcc's routine over each wrapper.
 *
 * The 68020 instructions are written as words, as the game is assembled
 * for the 68000.
 */

    .data
    .even
    .globl  _atari_cpu_l32
    .globl  _atari_cpu_l64
_atari_cpu_l32: .byte 0         | 1: a 68020 or later (muls.l, divu.l)
_atari_cpu_l64: .byte 0         | 1: a 68020 to 68040 (and their 64-bit forms)
    .even

    .text
    .even

/*
 * fixed FixedMul(fixed a, fixed b): watcom.h's atari_fixed_mul16 to the
 * bit, (|a| * |b| + 0x8000) >> 16 with a's and b's sign, as the game's
 * logic must give the same on every machine: on a 68020 to 68040 one
 * mulu.l of 64 bits; elsewhere the C (FixedMul68000, watcom.c).
 */
    .globl  _FixedMul
_FixedMul:
    tst.b   _atari_cpu_l64
    bne.s   .Lfm030
    | a 68000 (or 68060): three mulu.w, as atari_fixed_mul16 (watcom.h):
    | (ah*bh << 16) + al*bh + ah*bl + ((al*bl + 0x8000) >> 16), mod 2^32,
    | on the magnitudes, the sign after; d2 and d3 kept in a0 and a1
    move.l  d2,a0
    move.l  d3,a1
    move.l  4(sp),d0
    bpl.s   1f
    neg.l   d0
1:  move.l  8(sp),d1
    bpl.s   2f
    neg.l   d1
2:  move.w  d0,d2
    mulu.w  d1,d2                   | al * bl
    add.l   #0x8000,d2
    clr.w   d2
    swap    d2
    swap    d1
    move.w  d0,d3
    mulu.w  d1,d3                   | al * bh
    add.l   d3,d2
    swap    d0
    move.w  d0,d3
    mulu.w  d1,d3                   | ah * bh
    swap    d3
    clr.w   d3
    add.l   d3,d2
    swap    d1
    mulu.w  d1,d0                   | ah * bl
    add.l   d2,d0
    move.l  4(sp),d1
    move.l  8(sp),d2
    eor.l   d2,d1                   | the sign
    bpl.s   3f
    neg.l   d0
3:  move.l  a0,d2
    move.l  a1,d3
    rts
.Lfm030:
    move.l  d2,a1
    move.l  4(sp),d0
    move.l  8(sp),d1
    move.l  d0,d2
    eor.l   d1,d2
    move.l  d2,a0                   | the product's sign, in bit 31
    tst.l   d0
    bpl.s   1f
    neg.l   d0
1:  tst.l   d1
    bpl.s   2f
    neg.l   d1
2:  .word   0x4C01,0x0401           | mulu.l d1,d1:d0: |a| * |b|, 64 bits
    moveq   #0,d2
    add.l   #0x8000,d0
    addx.l  d2,d1                   | the carry into the high long
    swap    d0
    swap    d1
    move.w  d0,d1                   | bits 16-47 of the sum
    move.l  d1,d0
    move.l  a0,d1
    bpl.s   3f
    neg.l   d0
3:  move.l  a1,d2
    rts

/* One quotient digit of .Lfs_long: in d3 the remainder (under d2), the
 * next dividend digit in d4's high word (d4 is swapped); out the digit in
 * d0.w and the new remainder in d3. d1, d6, d7 are used. */
    .macro  FSDIGIT n
    move.l  d3,d0
    swap    d0
    cmp.w   d5,d0
    bcc.s   .Lfsd_max\n            | the remainder's top 16 bits are the divisor's
    move.l  d3,d0
    divu.w  d5,d0                   | the estimate, in d0.w
    bra.s   .Lfsd_have\n
.Lfsd_max\n:
    moveq   #-1,d0
.Lfsd_have\n:
    move.l  d3,d1
    clr.w   d1
    swap    d1                      | d1:d3, the 48 bits (r:digit)
    swap    d3
    swap    d4
    move.w  d4,d3
    move.w  d0,d7
    mulu.w  d2,d7                   | the estimate * the divisor's low 16 bits
    moveq   #0,d6
    sub.l   d7,d3
    subx.l  d6,d1
    move.w  d0,d7
    mulu.w  d5,d7                   | * its high 16 bits, 16 bits up
    move.l  d7,d6
    clr.w   d6
    swap    d6
    swap    d7
    clr.w   d7
    sub.l   d7,d3
    subx.l  d6,d1
.Lfsd_fix\n:
    tst.l   d1
    bpl.s   .Lfsd_ok\n
    subq.w  #1,d0                   | one over: add the divisor back
    moveq   #0,d6
    add.l   d2,d3
    addx.l  d6,d1
    bra.s   .Lfsd_fix\n
.Lfsd_ok\n:
    .endm

/*
 * fixed FixedScale(fixed a, fixed b, fixed c): (a * b) / c in 64 bits, the
 * quotient's low 32 bits, as watcom.c's C (FixedScaleC) gives it: the ray
 * cast's every wall hit. On a 68020 to 68040 one muls.l and one divs.l of
 * 64 bits; on a 68000 the product's four mulu.w and a division of 64 bits
 * by 32 (two divu.w for a divisor under 65536, else two estimated 16-bit
 * digits, .Lfs_long), on the magnitudes, with the sign after, as C rounds
 * toward zero. A quotient that needs more than 32 bits goes to the C.
 */
    .globl  _FixedScale
_FixedScale:
    tst.b   _atari_cpu_l64
    bne     .Lfs030
    movem.l d2-d7,-(sp)
    move.l  28(sp),d0               | a
    move.l  32(sp),d1               | b
    move.l  36(sp),d2               | c
    move.l  d0,d6
    eor.l   d1,d6
    eor.l   d2,d6                   | the quotient's sign, in bit 31
    tst.l   d0
    bpl.s   1f
    neg.l   d0
1:  tst.l   d1
    bpl.s   2f
    neg.l   d1
2:  tst.l   d2
    bpl.s   3f
    neg.l   d2
3:  move.w  d0,d4
    mulu.w  d1,d4                   | al * bl
    move.l  d0,d3
    swap    d3
    move.w  d3,d5
    mulu.w  d1,d5                   | ah * bl
    swap    d1
    mulu.w  d1,d3                   | ah * bh
    mulu.w  d0,d1                   | al * bh
    add.l   d5,d1                   | the middle, 33 bits
    bcc.s   4f
    add.l   #0x10000,d3
4:  move.l  d1,d5
    swap    d5
    clr.w   d5                      | its low half, up 16
    clr.w   d1
    swap    d1                      | its high half
    add.l   d5,d4
    addx.l  d1,d3                   | the product: d3:d4
    cmp.l   d2,d3
    bcc     .Lfs_c                  | a quotient of 32 bits or more (or c 0)
    cmp.l   #0xFFFF,d2
    bhi.s   .Lfs_long
    move.l  d4,d5
    swap    d5
    swap    d3
    move.w  d5,d3                   | the high long (under c) and 16 bits more
    divu.w  d2,d3
    move.w  d3,d0
    swap    d0
    move.w  d4,d3                   | the remainder and the last 16 bits
    divu.w  d2,d3
    move.w  d3,d0
    bra     .Lfs_sign

/*
 * The 64 bits d3:d4 (d3 < d2) by d2 of 17 bits or more: two quotient
 * digits of 16 bits (Knuth's algorithm D). The divisor is shifted up until
 * its top bit is set, and the dividend with it; each digit is estimated
 * from the remainder's top 32 bits over the divisor's top 16 (divu.w),
 * which is never under the digit and at most 2 over, then the remainder
 * (r:digit) - estimate * divisor is worked out in 64 bits and the divisor
 * added back while it is negative.
 */
.Lfs_long:
    move.l  d6,a1                   | the sign
    cmp.l   #0x00FFFFFF,d2
    bhi.s   1f
    lsl.l   #8,d2
    lsl.l   #8,d3
    rol.l   #8,d4
    move.b  d4,d3
    clr.b   d4
1:  tst.l   d2
    bmi.s   3f
2:  add.l   d4,d4
    addx.l  d3,d3
    add.l   d2,d2
    bpl.s   2b
3:  move.l  d2,d5
    swap    d5                      | d5.w: the divisor's top 16 bits
    FSDIGIT 1
    move.w  d0,a0                   | the high digit
    FSDIGIT 2
    move.l  a0,d1
    swap    d1
    move.w  d0,d1
    move.l  d1,d0
    move.l  a1,d6
.Lfs_sign:
    tst.l   d6
    bpl.s   5f
    neg.l   d0
5:  movem.l (sp)+,d2-d7
    rts
.Lfs_c:
    movem.l (sp)+,d2-d7
    bra     _FixedScaleC

.Lfs030:
    move.l  d2,a1
    move.l  4(sp),d0
    .word   0x4C2F,0x0C02,0x0008    | muls.l 8(sp),d2:d0: a * b, 64 bits
    .word   0x4C6F,0x0C02,0x000C    | divs.l 12(sp),d2:d0: by c
    bvs.s   6f
    move.l  a1,d2
    rts
6:  move.l  a1,d2
    bra     _FixedScaleC

/*
 * fixed atari_mulshift64(fixed a, fixed b, int shift): watcom.c's
 * FixedMulShift for a shift of 0 to 63 (not 16, which is atari_fixed_mul16
 * there): the signed 64-bit product shifted right logically, its low 32
 * bits. On a 68020 to 68040 one muls.l of 64 bits; on a 68000 the
 * magnitudes' four mulu.w and the sign after, in place of libgcc's
 * __muldi3 and a 64-bit shift.
 */
    .globl  _atari_mulshift64
_atari_mulshift64:
    move.l  d2,a1
    tst.b   _atari_cpu_l64
    beq.s   .Lms000
    move.l  4(sp),d0
    .word   0x4C2F,0x0C01,0x0008    | muls.l 8(sp),d1:d0: a * b, 64 bits
    bra.s   .Lms_shift
.Lms000:
    movem.l d3-d5,-(sp)
    move.l  16(sp),d0               | a
    move.l  20(sp),d1               | b
    move.l  d0,d5
    eor.l   d1,d5                   | the sign
    tst.l   d0
    bpl.s   1f
    neg.l   d0
1:  tst.l   d1
    bpl.s   2f
    neg.l   d1
2:  move.w  d0,d4
    mulu.w  d1,d4                   | al * bl
    move.l  d0,d3
    swap    d3
    move.w  d3,d2
    mulu.w  d1,d2                   | ah * bl
    swap    d1
    mulu.w  d1,d3                   | ah * bh
    mulu.w  d0,d1                   | al * bh
    add.l   d2,d1                   | the middle, 33 bits
    bcc.s   3f
    add.l   #0x10000,d3
3:  move.l  d1,d2
    swap    d2
    clr.w   d2
    clr.w   d1
    swap    d1
    add.l   d2,d4
    addx.l  d1,d3                   | |a| * |b|: d3:d4
    tst.l   d5
    bpl.s   4f
    neg.l   d4
    negx.l  d3
4:  move.l  d3,d1
    move.l  d4,d0
    movem.l (sp)+,d3-d5
.Lms_shift:
    move.l  12(sp),d2               | the shift, 0 to 63
    cmp.w   #32,d2
    bcc.s   5f
    lsr.l   d2,d0                   | (lo >> s) | (hi << (32 - s))
    neg.w   d2
    add.w   #32,d2
    lsl.l   d2,d1                   | a count of 32 leaves 0
    or.l    d1,d0
    move.l  a1,d2
    rts
5:  sub.w   #32,d2
    lsr.l   d2,d1
    move.l  d1,d0
    move.l  a1,d2
    rts
