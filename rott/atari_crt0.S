/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * atari_crt0.S — the native build's startup: libcmini's crt0 (startup.S),
 * which the link used before, and first of all a clear of the BSS.
 *
 * libcmini leaves clearing the BSS to the loader, and TOS does it: but on
 * a real Mega STE (TOS 2.06) a second run from the desktop started with
 * the first run's BSS still in place (LoadColorMap "called twice", the
 * player of the last game, the sound effects already "open"). The game
 * assumes every static starts at zero, so it makes sure of that itself.
 */

    .text
    .even

/* The entry: a program (a0 = 0, the basepage at 4(sp)), or a desk
 * accessory (a0 = its basepage, no parent). */
    .globl  __start
__start:
    sub.l   a6,a6
    cmp.l   #0,a0
    beq.s   __startprg
    tst.l   36(a0)
    bne.s   __startprg
    move.l  a0,__base
    lea     252(a0),sp
    jmp     __acc_main

    .globl  __startprg
__startprg:
    move.l  4(sp),a0            | the basepage

    | The BSS (p_bbase, p_blen) cleared, 16 bytes at a time, then the rest.
    | It lies below the stack, which TOS puts at the top of the TPA.
    move.l  24(a0),a1
    move.l  28(a0),d1
    moveq   #0,d0
    move.l  d1,d2
    lsr.l   #4,d2
    bra.s   2f
1:  move.l  d0,(a1)+
    move.l  d0,(a1)+
    move.l  d0,(a1)+
    move.l  d0,(a1)+
2:  subq.l  #1,d2
    bcc.s   1b
    and.w   #15,d1
    bra.s   4f
3:  move.b  d0,(a1)+
4:  dbra    d1,3b

    move.l  a0,__base
    move.l  4(a0),d0            | p_hitpa: the stack below the top of the TPA
    bclr    #0,d0
    move.l  d0,sp
    sub.l   #64,sp
    jmp     __crtinit

    .globl  ___mcleanup
___mcleanup:
    rts
