/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * atari_md_s.S - copy a frame the Multi-device rendered from its ROM4
 * window to the ST screen.
 *
 * The MD stores a frame as `rows` rows of `bpr` bytes of ST low-res planar
 * data (bpr = view width / 2, a multiple of 16), exactly as they go on the
 * screen, so the copy is plain MOVEM traffic: 48 bytes per load/store pair
 * (the 12 registers d0-d7/a2-a5, as md-doom's FBDRV_INLINE), a0/a1 as the
 * pointers and a6 as the counter.
 *
 * A full-width view is one contiguous block on both sides and takes the
 * fast path: four chunks per loop pass with displacement stores, so the
 * destination pointer moves once per 192 bytes (about 15 ms for a
 * 320 x 168 view on an 8 MHz ST). Narrower views go row by row.
 *
 * void atari_md_copy(const void *src, void *dst, int rows, int bpr);
 */

    .text
    .even

    .globl _atari_md_copy
_atari_md_copy:
    movem.l d2-d7/a2-a6,-(sp)
    move.l  48(sp),a0               | src (ROM4)
    move.l  52(sp),a1               | dst (screen)
    move.l  56(sp),d0               | rows
    move.l  60(sp),d1               | bytes per row
    tst.l   d0
    ble     .Ldone
    cmp.l   #160,d1
    bne     .Lrows

/* Full width: rows * 160 bytes, contiguous. 192-byte passes, then the
 * rest in 48 and 16 byte pieces. */
    mulu.w  #160,d0                 | total bytes (<= 26,880)
    move.l  d0,-(sp)
    divu.w  #192,d0                 | d0.w = passes, high word = remainder
    move.w  d0,a6
    subq.w  #1,a6
    bmi.s   .Lfull_rest
.Lfull_loop:
    movem.l (a0)+,d0-d7/a2-a5
    movem.l d0-d7/a2-a5,(a1)
    movem.l (a0)+,d0-d7/a2-a5
    movem.l d0-d7/a2-a5,48(a1)
    movem.l (a0)+,d0-d7/a2-a5
    movem.l d0-d7/a2-a5,96(a1)
    movem.l (a0)+,d0-d7/a2-a5
    movem.l d0-d7/a2-a5,144(a1)
    lea     192(a1),a1
    move.w  a6,d0
    subq.w  #1,d0
    move.w  d0,a6
    bpl.s   .Lfull_loop
.Lfull_rest:
    move.l  (sp)+,d0
    divu.w  #192,d0
    swap    d0                      | remainder, a multiple of 16
    ext.l   d0
    move.l  d0,d1
    bsr     copy_span
    bra.s   .Ldone

/* Row by row: the screen is 160 bytes a line, the MD's rows are bpr. */
.Lrows:
    move.l  d0,a6                   | rows
.Lrow_loop:
    move.l  a1,-(sp)
    bsr     copy_span               | copies d1 bytes, a0 and a1 advance
    move.l  (sp)+,a1
    lea     160(a1),a1
    subq.l  #1,a6
    move.l  a6,d0
    bne.s   .Lrow_loop

.Ldone:
    movem.l (sp)+,d2-d7/a2-a6
    rts

/* Copy d1 bytes (a multiple of 16) from (a0)+ to (a1)+.
 * Uses d0 and d2-d7/a2-a5; keeps d1 and a6. */
copy_span:
    move.l  d1,-(sp)
.Lspan48:
    cmp.l   #48,d1
    blt.s   .Lspan16
    movem.l (a0)+,d0/d2-d7/a2-a5
    movem.l d0/d2-d7/a2-a5,(a1)
    lea     44(a1),a1
    move.l  (a0)+,(a1)+
    sub.l   #48,d1
    bra.s   .Lspan48
.Lspan16:
    tst.l   d1
    beq.s   .Lspan_done
    move.l  (a0)+,(a1)+
    move.l  (a0)+,(a1)+
    move.l  (a0)+,(a1)+
    move.l  (a0)+,(a1)+
    sub.l   #16,d1
    bra.s   .Lspan16
.Lspan_done:
    move.l  (sp)+,d1
    rts
