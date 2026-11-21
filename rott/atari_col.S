/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * atari_col.S - the native build's column loops (rt_scale.c's R_Draw*
 * and rt_draw.c's DrawSkyPost): one pixel a row down the 320-wide chunky
 * screen, through a shading table.
 *
 * 68000 instructions alone, for every machine. A texture's position is
 * 16.16: its fraction lives in one word and the texel row in another,
 * stepped with add.w/addx.w, so a pixel is five instructions (48 cycles on
 * a 68000) where gcc's loop shifted a long by 26 or 16 every pixel (a
 * 68000 shifts a bit at a time). Eight pixels a pass with the rows as
 * displacements, entered part way for the odd ones (the tables below), so
 * the screen pointer moves once in eight. The pixels are the C loops', to
 * the bit: a wall's row is bits 16-21 of the position (64 texels, wrapping),
 * a sprite's bits 16 up.
 *
 * void atari_col_wall(byte *dest, const byte *src, const byte *shade,
 *                     long frac, long step, long count);
 * void atari_col_tex(same);
 * void atari_col_sky(byte *dest, const byte *src, const byte *shade, long count);
 * void atari_col_trans(byte *dest, const byte *shade, long count);
 * void atari_col_solid(byte *dest, long colour, long count);
 *
 * count is at least 1.
 */

#define ROW 320

    .text
    .even

/* d7 = count on entry: d7 = passes - 1, d6 = the pixel to enter at times
 * two (0 for a multiple of eight), a2 moved back by that many rows. */
    .macro  DUFF name
    move.l  d7,d6
    neg.w   d6
    and.w   #7,d6               | pixels to skip in the first pass
    addq.l  #7,d7
    lsr.l   #3,d7
    subq.w  #1,d7
    add.w   d6,d6
    sub.w   .L\name\()_back(pc,d6.w),a2
    .endm

/* the rows to move back, after each loop's jump table (within reach of a
 * PC-relative index) */
    .macro  BACK name
.L\name\()_back:
    .word   0, ROW, 2*ROW, 3*ROW, 4*ROW, 5*ROW, 6*ROW, 7*ROW
    .endm

/* ---------------------------------------------------------------- */

    .macro  WALLPIX n
.Lwall_\n:
    move.b  (a0,d1.w),d4
    move.b  (a1,d4.w),\n*ROW(a2)
    add.w   d2,d0
    addx.w  d3,d1
    and.w   d5,d1
    .endm

    .globl  _atari_col_wall
_atari_col_wall:
    movem.l d2-d7/a2,-(sp)
    move.l  32(sp),a2           | dest
    move.l  36(sp),a0           | texture column
    move.l  40(sp),a1           | shading table
    move.l  44(sp),d0           | position: row in the high word
    move.l  48(sp),d2           | step
    move.l  52(sp),d7           | count
    move.l  d0,d1
    swap    d1
    move.l  d2,d3
    swap    d3
    moveq   #63,d5
    and.w   d5,d1
    moveq   #0,d4
    DUFF    wall
    move.w  .Lwall_tab(pc,d6.w),d6
    jmp     .Lwall_tab(pc,d6.w)
.Lwall_tab:
    .word   .Lwall_0-.Lwall_tab, .Lwall_1-.Lwall_tab, .Lwall_2-.Lwall_tab, .Lwall_3-.Lwall_tab
    .word   .Lwall_4-.Lwall_tab, .Lwall_5-.Lwall_tab, .Lwall_6-.Lwall_tab, .Lwall_7-.Lwall_tab
    BACK    wall
    WALLPIX 0
    WALLPIX 1
    WALLPIX 2
    WALLPIX 3
    WALLPIX 4
    WALLPIX 5
    WALLPIX 6
    WALLPIX 7
    lea     8*ROW(a2),a2
    dbf     d7,.Lwall_0
    movem.l (sp)+,d2-d7/a2
    rts

/* ---------------------------------------------------------------- */
/*
 * void atari_col_walls(const void *first, const void *end);
 *
 * A frame's wall columns, one after another, from the notes the wall loop
 * left (atari_wallcol_t, rt_dr_a.h: 24 bytes, dest, texture column,
 * shading table, position, step, count, flags): no call, arguments or
 * register saves a column, and the wall loop's set-up and this loop each
 * have a 68030's instruction cache to themselves.
 *
 * A NULL shading table is a texture shaded beforehand (rt_draw.c's
 * ShadedWall), stored as words, each texel in both bytes: its rows are
 * read with the position doubled, so the whole part is twice the row plus
 * the fraction's top bit, and either byte of the word at that offset is
 * the texel. A byte at a time it wraps at 128 ($7F, keeping that bit);
 * with flags 1 each pixel is written twice, side by side, with one move.w
 * from the word at the offset made even ($7E, on a copy: the position
 * itself wraps at 65536, a multiple of 128). The rows are the C's,
 * floor(position) & 63.
 */
    .macro  WALLSPIX n
.Lwalls_\n:
    move.b  (a0,d1.w),d4
    move.b  (a1,d4.w),\n*ROW(a2)
    add.w   d2,d0
    addx.w  d3,d1
    and.w   d5,d1
    .endm

    .macro  WZPIX n
.Lwz_\n:
    move.b  (a0,d1.w),\n*ROW(a2)
    add.w   d2,d0
    addx.w  d3,d1
    and.w   d5,d1
    .endm

    .macro  WWPIX n
.Lww_\n:
    move.w  d1,d4
    and.w   d5,d4
    move.w  (a0,d4.w),\n*ROW(a2)
    add.w   d2,d0
    addx.w  d3,d1
    .endm

/* A 68020 or later, low detail's pairs: the word at twice the row read
 * with a scaled index, so the position is the C's own, masked to 64 rows:
 * four instructions for two pixels, where the 68000's loop takes five. */
    .macro  W3PIX n
.Lw3_\n:
    .word   0x3570,0x1200,\n*ROW    | move.w (0,a0,d1.w*2),n*ROW(a2)
    add.w   d2,d0
    addx.w  d3,d1
    and.w   d5,d1
    .endm

    .globl  _atari_col_walls
_atari_col_walls:
    movem.l d2-d7/a2-a5,-(sp)
    move.l  44(sp),a3
    move.l  48(sp),a4
    moveq   #0,d4
    cmp.l   a4,a3
    bcc     .Lwalls_done
.Lwalls_next:
    move.l  (a3)+,a2
    move.l  (a3)+,a0
    move.l  (a3)+,a1
    move.l  (a3)+,d0
    move.l  (a3)+,d2
    moveq   #0,d7
    move.w  (a3)+,d7
    move.w  (a3)+,a5                | flags: 1, each pixel twice
    cmp.w   #0,a1
    beq     .Lwz_setup
    moveq   #0,d4                   | (a word loop leaves bits in it)
    moveq   #63,d5
    move.l  d0,d1
    swap    d1
    and.w   d5,d1
    move.l  d2,d3
    swap    d3
    DUFF    walls
    move.w  .Lwalls_tab(pc,d6.w),d6
    jmp     .Lwalls_tab(pc,d6.w)
.Lwalls_tab:
    .word   .Lwalls_0-.Lwalls_tab, .Lwalls_1-.Lwalls_tab, .Lwalls_2-.Lwalls_tab, .Lwalls_3-.Lwalls_tab
    .word   .Lwalls_4-.Lwalls_tab, .Lwalls_5-.Lwalls_tab, .Lwalls_6-.Lwalls_tab, .Lwalls_7-.Lwalls_tab
    BACK    walls
    WALLSPIX 0
    WALLSPIX 1
    WALLSPIX 2
    WALLSPIX 3
    WALLSPIX 4
    WALLSPIX 5
    WALLSPIX 6
    WALLSPIX 7
    lea     8*ROW(a2),a2
    dbf     d7,.Lwalls_0
    cmp.l   a4,a3
    bcs     .Lwalls_next
    bra     .Lwalls_done

.Lwz_setup:
    cmp.w   #0,a5
    beq.s   .Lwz_one
    tst.b   _atari_cpu_l32
    bne     .Lw3_setup              | a 68020 or later: atari_col_walls' W3 loop
.Lwz_one:
    add.l   d0,d0                   | the position doubled
    add.l   d2,d2                   | and the step
    move.l  d0,d1
    swap    d1
    move.l  d2,d3
    swap    d3
    DUFF    wz
    cmp.w   #0,a5
    bne     .Lww_go
    moveq   #0x7F,d5
    and.w   d5,d1
    move.w  .Lwz_tab(pc,d6.w),d6
    jmp     .Lwz_tab(pc,d6.w)
.Lwz_tab:
    .word   .Lwz_0-.Lwz_tab, .Lwz_1-.Lwz_tab, .Lwz_2-.Lwz_tab, .Lwz_3-.Lwz_tab
    .word   .Lwz_4-.Lwz_tab, .Lwz_5-.Lwz_tab, .Lwz_6-.Lwz_tab, .Lwz_7-.Lwz_tab
    BACK    wz
    WZPIX   0
    WZPIX   1
    WZPIX   2
    WZPIX   3
    WZPIX   4
    WZPIX   5
    WZPIX   6
    WZPIX   7
    lea     8*ROW(a2),a2
    dbf     d7,.Lwz_0
    cmp.l   a4,a3
    bcs     .Lwalls_next
    bra     .Lwalls_done

.Lw3_setup:
    moveq   #63,d5
    move.l  d0,d1
    swap    d1
    and.w   d5,d1
    move.l  d2,d3
    swap    d3
    DUFF    w3
    move.w  .Lw3_tab(pc,d6.w),d6
    jmp     .Lw3_tab(pc,d6.w)
.Lw3_tab:
    .word   .Lw3_0-.Lw3_tab, .Lw3_1-.Lw3_tab, .Lw3_2-.Lw3_tab, .Lw3_3-.Lw3_tab
    .word   .Lw3_4-.Lw3_tab, .Lw3_5-.Lw3_tab, .Lw3_6-.Lw3_tab, .Lw3_7-.Lw3_tab
    BACK    w3
    W3PIX   0
    W3PIX   1
    W3PIX   2
    W3PIX   3
    W3PIX   4
    W3PIX   5
    W3PIX   6
    W3PIX   7
    lea     8*ROW(a2),a2
    dbf     d7,.Lw3_0
    cmp.l   a4,a3
    bcs     .Lwalls_next
    bra     .Lwalls_done

.Lww_go:
    moveq   #0x7E,d5
    move.w  .Lww_tab(pc,d6.w),d6
    jmp     .Lww_tab(pc,d6.w)
.Lww_tab:
    .word   .Lww_0-.Lww_tab, .Lww_1-.Lww_tab, .Lww_2-.Lww_tab, .Lww_3-.Lww_tab
    .word   .Lww_4-.Lww_tab, .Lww_5-.Lww_tab, .Lww_6-.Lww_tab, .Lww_7-.Lww_tab
    WWPIX   0
    WWPIX   1
    WWPIX   2
    WWPIX   3
    WWPIX   4
    WWPIX   5
    WWPIX   6
    WWPIX   7
    lea     8*ROW(a2),a2
    dbf     d7,.Lww_0
    cmp.l   a4,a3
    bcs     .Lwalls_next
.Lwalls_done:
    movem.l (sp)+,d2-d7/a2-a5
    rts

/* ---------------------------------------------------------------- */

    .macro  TEXPIX n
.Ltex_\n:
    move.b  (a0,d1.w),d4
    move.b  (a1,d4.w),\n*ROW(a2)
    add.w   d2,d0
    addx.w  d3,d1
    .endm

    .globl  _atari_col_tex
_atari_col_tex:
    movem.l d2-d7/a2,-(sp)
    move.l  32(sp),a2
    move.l  36(sp),a0
    move.l  40(sp),a1
    move.l  44(sp),d0
    move.l  48(sp),d2
    move.l  52(sp),d7
    move.l  d0,d1
    swap    d1
    move.l  d2,d3
    swap    d3
    moveq   #0,d4
    DUFF    tex
    move.w  .Ltex_tab(pc,d6.w),d6
    jmp     .Ltex_tab(pc,d6.w)
.Ltex_tab:
    .word   .Ltex_0-.Ltex_tab, .Ltex_1-.Ltex_tab, .Ltex_2-.Ltex_tab, .Ltex_3-.Ltex_tab
    .word   .Ltex_4-.Ltex_tab, .Ltex_5-.Ltex_tab, .Ltex_6-.Ltex_tab, .Ltex_7-.Ltex_tab
    BACK    tex
    TEXPIX  0
    TEXPIX  1
    TEXPIX  2
    TEXPIX  3
    TEXPIX  4
    TEXPIX  5
    TEXPIX  6
    TEXPIX  7
    lea     8*ROW(a2),a2
    dbf     d7,.Ltex_0
    movem.l (sp)+,d2-d7/a2
    rts

/* ---------------------------------------------------------------- */

    .macro  SKYPIX n
.Lsky_\n:
    move.b  (a0)+,d4
    move.b  (a1,d4.w),\n*ROW(a2)
    .endm

    .globl  _atari_col_sky
_atari_col_sky:
    movem.l d4/d6-d7/a2,-(sp)
    move.l  20(sp),a2
    move.l  24(sp),a0
    move.l  28(sp),a1
    move.l  32(sp),d7
    moveq   #0,d4
    DUFF    sky
    move.w  .Lsky_tab(pc,d6.w),d6
    jmp     .Lsky_tab(pc,d6.w)
.Lsky_tab:
    .word   .Lsky_0-.Lsky_tab, .Lsky_1-.Lsky_tab, .Lsky_2-.Lsky_tab, .Lsky_3-.Lsky_tab
    .word   .Lsky_4-.Lsky_tab, .Lsky_5-.Lsky_tab, .Lsky_6-.Lsky_tab, .Lsky_7-.Lsky_tab
    BACK    sky
    SKYPIX  0
    SKYPIX  1
    SKYPIX  2
    SKYPIX  3
    SKYPIX  4
    SKYPIX  5
    SKYPIX  6
    SKYPIX  7
    lea     8*ROW(a2),a2
    dbf     d7,.Lsky_0
    movem.l (sp)+,d4/d6-d7/a2
    rts

/* ---------------------------------------------------------------- */

    .macro  TRANSPIX n
.Ltrans_\n:
    move.b  \n*ROW(a2),d4
    move.b  (a1,d4.w),\n*ROW(a2)
    .endm

    .globl  _atari_col_trans
_atari_col_trans:
    movem.l d4/d6-d7/a2,-(sp)
    move.l  20(sp),a2
    move.l  24(sp),a1
    move.l  28(sp),d7
    moveq   #0,d4
    DUFF    trans
    move.w  .Ltrans_tab(pc,d6.w),d6
    jmp     .Ltrans_tab(pc,d6.w)
.Ltrans_tab:
    .word   .Ltrans_0-.Ltrans_tab, .Ltrans_1-.Ltrans_tab, .Ltrans_2-.Ltrans_tab, .Ltrans_3-.Ltrans_tab
    .word   .Ltrans_4-.Ltrans_tab, .Ltrans_5-.Ltrans_tab, .Ltrans_6-.Ltrans_tab, .Ltrans_7-.Ltrans_tab
    BACK    trans
    TRANSPIX 0
    TRANSPIX 1
    TRANSPIX 2
    TRANSPIX 3
    TRANSPIX 4
    TRANSPIX 5
    TRANSPIX 6
    TRANSPIX 7
    lea     8*ROW(a2),a2
    dbf     d7,.Ltrans_0
    movem.l (sp)+,d4/d6-d7/a2
    rts

/* ---------------------------------------------------------------- */

    .macro  SOLIDPIX n
.Lsolid_\n:
    move.b  d0,\n*ROW(a2)
    .endm

    .globl  _atari_col_solid
_atari_col_solid:
    movem.l d6-d7/a2,-(sp)
    move.l  16(sp),a2
    move.l  20(sp),d0
    move.l  24(sp),d7
    DUFF    solid
    move.w  .Lsolid_tab(pc,d6.w),d6
    jmp     .Lsolid_tab(pc,d6.w)
.Lsolid_tab:
    .word   .Lsolid_0-.Lsolid_tab, .Lsolid_1-.Lsolid_tab, .Lsolid_2-.Lsolid_tab, .Lsolid_3-.Lsolid_tab
    .word   .Lsolid_4-.Lsolid_tab, .Lsolid_5-.Lsolid_tab, .Lsolid_6-.Lsolid_tab, .Lsolid_7-.Lsolid_tab
    BACK    solid
    SOLIDPIX 0
    SOLIDPIX 1
    SOLIDPIX 2
    SOLIDPIX 3
    SOLIDPIX 4
    SOLIDPIX 5
    SOLIDPIX 6
    SOLIDPIX 7
    lea     8*ROW(a2),a2
    dbf     d7,.Lsolid_0
    movem.l (sp)+,d6-d7/a2
    rts

/* ---------------------------------------------------------------- */
/*
 * void atari_post_masked(const byte *post, byte *buf);
 *
 * rt_scale.c's ScaleMaskedPost with R_DrawColumn inside it: a column of a
 * sprite, the weapon or a masked wall, as posts of {offset, length,
 * pixels...} ending in an offset of 255. For each: where it starts and
 * ends on screen (dc_invscale times its offset and its length, from
 * sprtopoffset), clipped to the view, then its pixels as atari_col_tex
 * draws them. The products are mulu.w (two each, one when the scale is
 * under 1.0) where the C made library calls, and nothing goes through the
 * dc_ globals or a call a post: the same pixels.
 *
 * Reads sprtopoffset, dc_invscale, dc_iscale, dc_texturemid, centery,
 * viewheight, shadingtable and ylookup[]. Leaves dc_yl, dc_yh and
 * dc_source alone (nothing reads them after a post).
 */

    .macro  PMPIX n
.Lpm_\n:
    move.b  (a0,d1.w),d4
    move.b  (a1,d4.w),\n*ROW(a2)
    add.w   d2,d0
    addx.w  d3,d1
    .endm

/* The lowest view row atari_post_masked or _trans last drew, -1 for none:
 * their C wrappers mark the column's rows for low detail's pairs
 * (atari_unpair_col, atari_c2p.h). */
    .data
    .even
    .globl  _atari_post_maxrow
_atari_post_maxrow: .word -1
    .text
    .even
    .globl  _atari_post_masked
_atari_post_masked:
    movem.l d2-d7/a2-a6,-(sp)
    move.w  #-1,_atari_post_maxrow
    move.l  48(sp),a3               | the posts
    move.l  52(sp),a4               | the column's top on the chunky screen
    move.l  _shadingtable,a1
    move.l  _sprtopoffset,a5
    move.w  #256,a6                 | spans from this offset on are below the view
    moveq   #0,d0
    move.b  (a3)+,d0                | offset
    cmp.w   #255,d0
    beq     .Lpm_done
.Lpm_post:
    moveq   #0,d1
    move.b  (a3)+,d1                | length
    cmp.w   a6,d0
    bcc     .Lpm_below
    move.w  _dc_invscale+2,d2
    mulu.w  d0,d2
    move.w  _dc_invscale,d3
    beq.s   1f
    mulu.w  d0,d3
    swap    d3
    clr.w   d3
    add.l   d3,d2
1:  add.l   a5,d2                   | topscreen
    move.l  d2,d4
    swap    d4
    addq.w  #1,d4
    cmp.w   _viewheight+2,d4
    bge     .Lpm_low                | dc_yl past the view: none of it
    move.w  _dc_invscale+2,d3
    mulu.w  d1,d3
    move.w  _dc_invscale,d4
    beq.s   2f
    mulu.w  d1,d4
    swap    d4
    clr.w   d4
    add.l   d4,d3
2:  add.l   d2,d3                   | bottomscreen
    move.l  a3,a0
    sub.w   d0,a0                   | the column: row `offset` is this post's first pixel
    add.w   d1,a3                   | the next post
    swap    d2
    addq.w  #1,d2                   | dc_yl = (topscreen + 1.0) >> 16
    subq.l  #1,d3
    swap    d3                      | dc_yh = (bottomscreen - 1) >> 16
    move.w  _viewheight+2,d4
    cmp.w   d4,d3
    blt.s   3f
    move.w  d4,d3
    subq.w  #1,d3
3:  tst.w   d2
    bpl.s   4f
    moveq   #0,d2
4:  cmp.w   d3,d2
    bgt     .Lpm_next               | none of it in the view
    cmp.w   _atari_post_maxrow,d3
    ble.s   10f
    move.w  d3,_atari_post_maxrow   | the lowest row drawn
10: sub.w   d2,d3
    addq.w  #1,d3
    moveq   #0,d7
    move.w  d3,d7                   | rows
    move.w  d2,d4
    add.w   d4,d4
    add.w   d4,d4
    lea     _ylookup,a2
    move.l  (a2,d4.w),a2
    add.l   a4,a2                   | the first pixel's place
    sub.w   _centery+2,d2           | rows from the centre, signed
    smi     d5
    bpl.s   5f
    neg.w   d2
5:  move.w  _dc_iscale+2,d0
    mulu.w  d2,d0
    move.w  _dc_iscale,d1
    beq.s   6f
    mulu.w  d2,d1
    swap    d1
    clr.w   d1
    add.l   d1,d0
6:  tst.b   d5
    beq.s   7f
    neg.l   d0
7:  add.l   _dc_texturemid,d0       | the first pixel's texture position
    move.l  _dc_iscale,d2           | and the step
    move.l  d0,d1
    swap    d1
    move.l  d2,d3
    swap    d3
    moveq   #0,d4
    DUFF    pm
    move.w  .Lpm_tab(pc,d6.w),d6
    jmp     .Lpm_tab(pc,d6.w)
.Lpm_tab:
    .word   .Lpm_0-.Lpm_tab, .Lpm_1-.Lpm_tab, .Lpm_2-.Lpm_tab, .Lpm_3-.Lpm_tab
    .word   .Lpm_4-.Lpm_tab, .Lpm_5-.Lpm_tab, .Lpm_6-.Lpm_tab, .Lpm_7-.Lpm_tab
    BACK    pm
    PMPIX   0
    PMPIX   1
    PMPIX   2
    PMPIX   3
    PMPIX   4
    PMPIX   5
    PMPIX   6
    PMPIX   7
    lea     8*ROW(a2),a2
    dbf     d7,.Lpm_0
.Lpm_next:
    moveq   #0,d0
    move.b  (a3)+,d0
    cmp.w   #255,d0
    bne     .Lpm_post
.Lpm_done:
    movem.l (sp)+,d2-d7/a2-a6
    rts
/* Below the view: and, where its top cannot have overflowed (dc_invscale
 * under 2^22, sprtopoffset under 2^30), every later span from as far down
 * its column is too: their tops are no higher. */
.Lpm_low:
    move.w  _dc_invscale,d4
    cmp.w   #0x40,d4
    bcc.s   .Lpm_below
    cmp.l   #0x40000000,a5
    bge.s   .Lpm_below
    move.w  d0,a6
.Lpm_below:
    add.w   d1,a3                   | past its pixels
    bra     .Lpm_next

/* ---------------------------------------------------------------- */
/*
 * void atari_post_trans(const byte *post, byte *buf, const byte *seelevel);
 *
 * rt_scale.c's ScaleTransparentPost: atari_post_masked's posts, except
 * that a post whose first byte is 254 has no pixels, and its rows are
 * shaded where they stand through seelevel (R_TransColumn). The same rows
 * and pixels as the C.
 */
    .macro  PTPIX n
.Lpt_\n:
    move.b  (a0,d1.w),d4
    move.b  (a1,d4.w),\n*ROW(a2)
    add.w   d2,d0
    addx.w  d3,d1
    .endm

    .macro  PTSPIX n
.Lpts_\n:
    move.b  \n*ROW(a2),d4
    move.b  (a6,d4.w),\n*ROW(a2)
    .endm

    .globl  _atari_post_trans
_atari_post_trans:
    movem.l d2-d7/a2-a6,-(sp)
    move.w  #-1,_atari_post_maxrow
    move.l  48(sp),a3               | the posts
    move.l  52(sp),a4               | the column's top
    move.l  56(sp),a6               | seelevel
    move.l  _shadingtable,a1
    move.l  _sprtopoffset,a5
    moveq   #0,d0
    move.b  (a3)+,d0                | offset
    cmp.w   #255,d0
    beq     .Lpt_done
.Lpt_post:
    moveq   #0,d1
    move.b  (a3)+,d1                | length
    move.w  _dc_invscale+2,d2
    mulu.w  d0,d2
    move.w  _dc_invscale,d3
    beq.s   1f
    mulu.w  d0,d3
    swap    d3
    clr.w   d3
    add.l   d3,d2
1:  add.l   a5,d2                   | topscreen
    move.w  _dc_invscale+2,d3
    mulu.w  d1,d3
    move.w  _dc_invscale,d4
    beq.s   2f
    mulu.w  d1,d4
    swap    d4
    clr.w   d4
    add.l   d4,d3
2:  add.l   d2,d3                   | bottomscreen
    move.l  a3,a0
    sub.w   d0,a0                   | the column, for a post with pixels
    moveq   #0,d5                   | 0: pixels; 1: shaded where they stand
    cmp.b   #254,(a3)
    bne.s   3f
    moveq   #1,d5
    addq.l  #1,a3                   | past the marker
    bra.s   4f
3:  add.w   d1,a3                   | past the pixels
4:  swap    d2
    addq.w  #1,d2                   | dc_yl
    subq.l  #1,d3
    swap    d3                      | dc_yh
    move.w  _viewheight+2,d4
    cmp.w   d4,d3
    blt.s   5f
    move.w  d4,d3
    subq.w  #1,d3
5:  tst.w   d2
    bpl.s   6f
    moveq   #0,d2
6:  cmp.w   d3,d2
    bgt     .Lpt_next
    cmp.w   _atari_post_maxrow,d3
    ble.s   10f
    move.w  d3,_atari_post_maxrow   | the lowest row drawn
10: sub.w   d2,d3
    addq.w  #1,d3
    moveq   #0,d7
    move.w  d3,d7                   | rows
    move.w  d2,d4
    add.w   d4,d4
    add.w   d4,d4
    lea     _ylookup,a2
    move.l  (a2,d4.w),a2
    add.l   a4,a2
    tst.b   d5
    bne     .Lpts_go
    sub.w   _centery+2,d2
    smi     d5
    bpl.s   7f
    neg.w   d2
7:  move.w  _dc_iscale+2,d0
    mulu.w  d2,d0
    move.w  _dc_iscale,d1
    beq.s   8f
    mulu.w  d2,d1
    swap    d1
    clr.w   d1
    add.l   d1,d0
8:  tst.b   d5
    beq.s   9f
    neg.l   d0
9:  add.l   _dc_texturemid,d0
    move.l  _dc_iscale,d2
    move.l  d0,d1
    swap    d1
    move.l  d2,d3
    swap    d3
    moveq   #0,d4
    DUFF    pt
    move.w  .Lpt_tab(pc,d6.w),d6
    jmp     .Lpt_tab(pc,d6.w)
.Lpt_tab:
    .word   .Lpt_0-.Lpt_tab, .Lpt_1-.Lpt_tab, .Lpt_2-.Lpt_tab, .Lpt_3-.Lpt_tab
    .word   .Lpt_4-.Lpt_tab, .Lpt_5-.Lpt_tab, .Lpt_6-.Lpt_tab, .Lpt_7-.Lpt_tab
    BACK    pt
    PTPIX   0
    PTPIX   1
    PTPIX   2
    PTPIX   3
    PTPIX   4
    PTPIX   5
    PTPIX   6
    PTPIX   7
    lea     8*ROW(a2),a2
    dbf     d7,.Lpt_0
    bra     .Lpt_next

.Lpts_go:
    moveq   #0,d4
    DUFF    pts
    move.w  .Lpts_tab(pc,d6.w),d6
    jmp     .Lpts_tab(pc,d6.w)
.Lpts_tab:
    .word   .Lpts_0-.Lpts_tab, .Lpts_1-.Lpts_tab, .Lpts_2-.Lpts_tab, .Lpts_3-.Lpts_tab
    .word   .Lpts_4-.Lpts_tab, .Lpts_5-.Lpts_tab, .Lpts_6-.Lpts_tab, .Lpts_7-.Lpts_tab
    BACK    pts
    PTSPIX  0
    PTSPIX  1
    PTSPIX  2
    PTSPIX  3
    PTSPIX  4
    PTSPIX  5
    PTSPIX  6
    PTSPIX  7
    lea     8*ROW(a2),a2
    dbf     d7,.Lpts_0

.Lpt_next:
    moveq   #0,d0
    move.b  (a3)+,d0
    cmp.w   #255,d0
    bne     .Lpt_post
.Lpt_done:
    movem.l (sp)+,d2-d7/a2-a6
    rts

/* ---------------------------------------------------------------- */
/*
 * void atari_row(long count, byte *dest, const byte *flat, const byte *shade,
 *                unsigned long frac, unsigned long step);
 *
 * rt_floor.c's DrawRow: a row of floor or ceiling from a 128x128 flat.
 * The position is the C's 32 bits, y in the high word and x in the low,
 * stepped as one (add.w then addx.w, so x's carry reaches y as it did),
 * and the texel is ((x >> 9) << 7) | (y >> 9): x's top 7 bits by a shift of
 * 2 and a mask, y's by a rotate of 7, where gcc shifted the long by 9 and
 * by 25 and read shadingtable again for every pixel. count is at least 1.
 */
    .globl  _atari_row
_atari_row:
    movem.l d2-d7/a2,-(sp)
    move.l  32(sp),d7               | count
    move.l  36(sp),a2               | dest
    move.l  40(sp),a0               | the flat
    move.l  44(sp),a1               | the shading table
    move.l  48(sp),d1               | position
    move.l  52(sp),d3               | step
    move.w  d1,d0                   | x
    swap    d1                      | y
    move.w  d3,d2                   | x's step
    swap    d3                      | y's step
    move.w  #0x3F80,d6
    moveq   #0,d5
    subq.w  #1,d7
.Lrow:
    move.w  d1,d5
    rol.w   #7,d5
    and.w   #127,d5                 | y >> 9
    move.w  d0,d4
    lsr.w   #2,d4
    and.w   d6,d4                   | (x >> 9) << 7
    or.w    d5,d4
    move.b  (a0,d4.w),d5
    move.b  (a1,d5.w),(a2)+
    add.w   d2,d0
    addx.w  d3,d1
    dbf     d7,.Lrow
    movem.l (sp)+,d2-d7/a2
    rts

/* ---------------------------------------------------------------- */
/*
 * Low detail's sky and floors, as DOS ROTT draws them (ATARI_LOWDETAIL_FLATS,
 * rt_floor.c): a pixel fetched and shaded once and stored in both columns
 * of a pair.
 *
 * void atari_col_sky2(byte *dest, const byte *src, const byte *shade, long count);
 * void atari_row2(long pairs, byte *dest, const byte *flat, const byte *shade,
 *                 unsigned long frac, unsigned long step);
 */
    .macro  SKY2PIX n
.Lsky2_\n:
    move.b  (a0)+,d4
    move.b  (a1,d4.w),d5
    move.b  d5,\n*ROW(a2)
    move.b  d5,\n*ROW+1(a2)
    .endm

    .globl  _atari_col_sky2
_atari_col_sky2:
    movem.l d4-d7/a2,-(sp)
    move.l  24(sp),a2
    move.l  28(sp),a0
    move.l  32(sp),a1
    move.l  36(sp),d7
    moveq   #0,d4
    DUFF    sky2
    move.w  .Lsky2_tab(pc,d6.w),d6
    jmp     .Lsky2_tab(pc,d6.w)
.Lsky2_tab:
    .word   .Lsky2_0-.Lsky2_tab, .Lsky2_1-.Lsky2_tab, .Lsky2_2-.Lsky2_tab, .Lsky2_3-.Lsky2_tab
    .word   .Lsky2_4-.Lsky2_tab, .Lsky2_5-.Lsky2_tab, .Lsky2_6-.Lsky2_tab, .Lsky2_7-.Lsky2_tab
    BACK    sky2
    SKY2PIX 0
    SKY2PIX 1
    SKY2PIX 2
    SKY2PIX 3
    SKY2PIX 4
    SKY2PIX 5
    SKY2PIX 6
    SKY2PIX 7
    lea     8*ROW(a2),a2
    dbf     d7,.Lsky2_0
    movem.l (sp)+,d4-d7/a2
    rts

    .globl  _atari_row2
_atari_row2:
    movem.l d2-d7/a2,-(sp)
    move.l  32(sp),d7               | pairs
    move.l  36(sp),a2
    move.l  40(sp),a0
    move.l  44(sp),a1
    move.l  48(sp),d1
    move.l  52(sp),d3
    move.w  d1,d0
    swap    d1
    move.w  d3,d2
    swap    d3
    move.w  #0x3F80,d6
    moveq   #0,d5
    subq.w  #1,d7
.Lrow2:
    move.w  d1,d5
    rol.w   #7,d5
    and.w   #127,d5
    move.w  d0,d4
    lsr.w   #2,d4
    and.w   d6,d4
    or.w    d5,d4
    move.b  (a0,d4.w),d5
    move.b  (a1,d5.w),d5
    move.b  d5,(a2)+
    move.b  d5,(a2)+
    add.w   d2,d0
    addx.w  d3,d1
    dbf     d7,.Lrow2
    movem.l (sp)+,d2-d7/a2
    rts

/* ---------------------------------------------------------------- */
/*
 * atari_post_masked2, atari_post_trans2: the same, for low detail's masked
 * walls (ATARI_LOWDETAIL_MASKED, rt_draw.c): each pixel fetched and shaded
 * once and stored in two columns side by side; a translucent span shades
 * each column's own pixels.
 */
    .macro  PM2PIX n
.Lpm2_\n:
    move.b  (a0,d1.w),d4
    move.b  (a1,d4.w),d6
    move.b  d6,\n*ROW(a2)
    move.b  d6,\n*ROW+1(a2)
    add.w   d2,d0
    addx.w  d3,d1
    .endm

    .globl  _atari_post_masked2
_atari_post_masked2:
    movem.l d2-d7/a2-a6,-(sp)
    move.l  48(sp),a3               | the posts
    move.l  52(sp),a4               | the column's top on the chunky screen
    move.l  _shadingtable,a1
    move.l  _sprtopoffset,a5
    move.w  #256,a6                 | spans from this offset on are below the view
    moveq   #0,d0
    move.b  (a3)+,d0                | offset
    cmp.w   #255,d0
    beq     .Lpm2_done
.Lpm2_post:
    moveq   #0,d1
    move.b  (a3)+,d1                | length
    cmp.w   a6,d0
    bcc     .Lpm2_below
    move.w  _dc_invscale+2,d2
    mulu.w  d0,d2
    move.w  _dc_invscale,d3
    beq.s   1f
    mulu.w  d0,d3
    swap    d3
    clr.w   d3
    add.l   d3,d2
1:  add.l   a5,d2                   | topscreen
    move.l  d2,d4
    swap    d4
    addq.w  #1,d4
    cmp.w   _viewheight+2,d4
    bge     .Lpm2_low                | dc_yl past the view: none of it
    move.w  _dc_invscale+2,d3
    mulu.w  d1,d3
    move.w  _dc_invscale,d4
    beq.s   2f
    mulu.w  d1,d4
    swap    d4
    clr.w   d4
    add.l   d4,d3
2:  add.l   d2,d3                   | bottomscreen
    move.l  a3,a0
    sub.w   d0,a0                   | the column: row `offset` is this post's first pixel
    add.w   d1,a3                   | the next post
    swap    d2
    addq.w  #1,d2                   | dc_yl = (topscreen + 1.0) >> 16
    subq.l  #1,d3
    swap    d3                      | dc_yh = (bottomscreen - 1) >> 16
    move.w  _viewheight+2,d4
    cmp.w   d4,d3
    blt.s   3f
    move.w  d4,d3
    subq.w  #1,d3
3:  tst.w   d2
    bpl.s   4f
    moveq   #0,d2
4:  cmp.w   d3,d2
    bgt     .Lpm2_next               | none of it in the view
    sub.w   d2,d3
    addq.w  #1,d3
    moveq   #0,d7
    move.w  d3,d7                   | rows
    move.w  d2,d4
    add.w   d4,d4
    add.w   d4,d4
    lea     _ylookup,a2
    move.l  (a2,d4.w),a2
    add.l   a4,a2                   | the first pixel's place
    sub.w   _centery+2,d2           | rows from the centre, signed
    smi     d5
    bpl.s   5f
    neg.w   d2
5:  move.w  _dc_iscale+2,d0
    mulu.w  d2,d0
    move.w  _dc_iscale,d1
    beq.s   6f
    mulu.w  d2,d1
    swap    d1
    clr.w   d1
    add.l   d1,d0
6:  tst.b   d5
    beq.s   7f
    neg.l   d0
7:  add.l   _dc_texturemid,d0       | the first pixel's texture position
    move.l  _dc_iscale,d2           | and the step
    move.l  d0,d1
    swap    d1
    move.l  d2,d3
    swap    d3
    moveq   #0,d4
    DUFF    pm2
    move.w  .Lpm2_tab(pc,d6.w),d6
    jmp     .Lpm2_tab(pc,d6.w)
.Lpm2_tab:
    .word   .Lpm2_0-.Lpm2_tab, .Lpm2_1-.Lpm2_tab, .Lpm2_2-.Lpm2_tab, .Lpm2_3-.Lpm2_tab
    .word   .Lpm2_4-.Lpm2_tab, .Lpm2_5-.Lpm2_tab, .Lpm2_6-.Lpm2_tab, .Lpm2_7-.Lpm2_tab
    BACK    pm2
    PM2PIX   0
    PM2PIX   1
    PM2PIX   2
    PM2PIX   3
    PM2PIX   4
    PM2PIX   5
    PM2PIX   6
    PM2PIX   7
    lea     8*ROW(a2),a2
    dbf     d7,.Lpm2_0
.Lpm2_next:
    moveq   #0,d0
    move.b  (a3)+,d0
    cmp.w   #255,d0
    bne     .Lpm2_post
.Lpm2_done:
    movem.l (sp)+,d2-d7/a2-a6
    rts
/* Below the view: and, where its top cannot have overflowed (dc_invscale
 * under 2^22, sprtopoffset under 2^30), every later span from as far down
 * its column is too: their tops are no higher. */
.Lpm2_low:
    move.w  _dc_invscale,d4
    cmp.w   #0x40,d4
    bcc.s   .Lpm2_below
    cmp.l   #0x40000000,a5
    bge.s   .Lpm2_below
    move.w  d0,a6
.Lpm2_below:
    add.w   d1,a3                   | past its pixels
    bra     .Lpm2_next

    .macro  PT2PIX n
.Lpt2_\n:
    move.b  (a0,d1.w),d4
    move.b  (a1,d4.w),d6
    move.b  d6,\n*ROW(a2)
    move.b  d6,\n*ROW+1(a2)
    add.w   d2,d0
    addx.w  d3,d1
    .endm

    .macro  PTS2PIX n
.Lpts2_\n:
    move.b  \n*ROW(a2),d4
    move.b  (a6,d4.w),\n*ROW(a2)
    move.b  \n*ROW+1(a2),d4
    move.b  (a6,d4.w),\n*ROW+1(a2)
    .endm

    .globl  _atari_post_trans2
_atari_post_trans2:
    movem.l d2-d7/a2-a6,-(sp)
    move.l  48(sp),a3               | the posts
    move.l  52(sp),a4               | the column's top
    move.l  56(sp),a6               | seelevel
    move.l  _shadingtable,a1
    move.l  _sprtopoffset,a5
    moveq   #0,d0
    move.b  (a3)+,d0                | offset
    cmp.w   #255,d0
    beq     .Lpt2_done
.Lpt2_post:
    moveq   #0,d1
    move.b  (a3)+,d1                | length
    move.w  _dc_invscale+2,d2
    mulu.w  d0,d2
    move.w  _dc_invscale,d3
    beq.s   1f
    mulu.w  d0,d3
    swap    d3
    clr.w   d3
    add.l   d3,d2
1:  add.l   a5,d2                   | topscreen
    move.w  _dc_invscale+2,d3
    mulu.w  d1,d3
    move.w  _dc_invscale,d4
    beq.s   2f
    mulu.w  d1,d4
    swap    d4
    clr.w   d4
    add.l   d4,d3
2:  add.l   d2,d3                   | bottomscreen
    move.l  a3,a0
    sub.w   d0,a0                   | the column, for a post with pixels
    moveq   #0,d5                   | 0: pixels; 1: shaded where they stand
    cmp.b   #254,(a3)
    bne.s   3f
    moveq   #1,d5
    addq.l  #1,a3                   | past the marker
    bra.s   4f
3:  add.w   d1,a3                   | past the pixels
4:  swap    d2
    addq.w  #1,d2                   | dc_yl
    subq.l  #1,d3
    swap    d3                      | dc_yh
    move.w  _viewheight+2,d4
    cmp.w   d4,d3
    blt.s   5f
    move.w  d4,d3
    subq.w  #1,d3
5:  tst.w   d2
    bpl.s   6f
    moveq   #0,d2
6:  cmp.w   d3,d2
    bgt     .Lpt2_next
    sub.w   d2,d3
    addq.w  #1,d3
    moveq   #0,d7
    move.w  d3,d7                   | rows
    move.w  d2,d4
    add.w   d4,d4
    add.w   d4,d4
    lea     _ylookup,a2
    move.l  (a2,d4.w),a2
    add.l   a4,a2
    tst.b   d5
    bne     .Lpts2_go
    sub.w   _centery+2,d2
    smi     d5
    bpl.s   7f
    neg.w   d2
7:  move.w  _dc_iscale+2,d0
    mulu.w  d2,d0
    move.w  _dc_iscale,d1
    beq.s   8f
    mulu.w  d2,d1
    swap    d1
    clr.w   d1
    add.l   d1,d0
8:  tst.b   d5
    beq.s   9f
    neg.l   d0
9:  add.l   _dc_texturemid,d0
    move.l  _dc_iscale,d2
    move.l  d0,d1
    swap    d1
    move.l  d2,d3
    swap    d3
    moveq   #0,d4
    DUFF    pt2
    move.w  .Lpt2_tab(pc,d6.w),d6
    jmp     .Lpt2_tab(pc,d6.w)
.Lpt2_tab:
    .word   .Lpt2_0-.Lpt2_tab, .Lpt2_1-.Lpt2_tab, .Lpt2_2-.Lpt2_tab, .Lpt2_3-.Lpt2_tab
    .word   .Lpt2_4-.Lpt2_tab, .Lpt2_5-.Lpt2_tab, .Lpt2_6-.Lpt2_tab, .Lpt2_7-.Lpt2_tab
    BACK    pt2
    PT2PIX   0
    PT2PIX   1
    PT2PIX   2
    PT2PIX   3
    PT2PIX   4
    PT2PIX   5
    PT2PIX   6
    PT2PIX   7
    lea     8*ROW(a2),a2
    dbf     d7,.Lpt2_0
    bra     .Lpt2_next

.Lpts2_go:
    moveq   #0,d4
    DUFF    pts2
    move.w  .Lpts2_tab(pc,d6.w),d6
    jmp     .Lpts2_tab(pc,d6.w)
.Lpts2_tab:
    .word   .Lpts2_0-.Lpts2_tab, .Lpts2_1-.Lpts2_tab, .Lpts2_2-.Lpts2_tab, .Lpts2_3-.Lpts2_tab
    .word   .Lpts2_4-.Lpts2_tab, .Lpts2_5-.Lpts2_tab, .Lpts2_6-.Lpts2_tab, .Lpts2_7-.Lpts2_tab
    BACK    pts2
    PTS2PIX  0
    PTS2PIX  1
    PTS2PIX  2
    PTS2PIX  3
    PTS2PIX  4
    PTS2PIX  5
    PTS2PIX  6
    PTS2PIX  7
    lea     8*ROW(a2),a2
    dbf     d7,.Lpts2_0

.Lpt2_next:
    moveq   #0,d0
    move.b  (a3)+,d0
    cmp.w   #255,d0
    bne     .Lpt2_post
.Lpt2_done:
    movem.l (sp)+,d2-d7/a2-a6
    rts

/* ---------------------------------------------------------------- */
/*
 * wallcast_t *atari_walls_run(atari_wallrun_t *ctx);
 *
 * rt_draw.c's DrawWallsFast for its usual post: a wall with no alternate
 * tile, shaded beforehand, of a height under 65536 (with the eye's height
 * and the wall's in 16 bits: ctx->allowed). Each is set up as the C sets
 * it up, the same arithmetic in the same order, and its column noted for
 * atari_col_walls; the first post it does not take (or the end) goes back
 * to the C, which does that one and calls again. ctx (rt_draw.c keeps it
 * in step): 0 post, 4 end, 8 buf, 12 the note, 16 the notes' limit,
 * 20 shadedwall, 24 step, 28 twice, 32 viewheight, 36 centery,
 * 40 texmid, 44 centeryfrac, 48 pheight, 52 maxheight + 32, 56 allowed,
 * 60 gh, 64 ght, 68 ginv, 72 gtop, 76 gbot (the geometry's height, ht
 * and values), 80 ih, 84 iscale (the step's height and value).
 */
    .globl  _atari_walls_run
_atari_walls_run:
    movem.l d2-d7/a2-a6,-(sp)
    move.l  48(sp),a2               | ctx
    move.l  (a2),a4                 | post
    move.l  4(a2),a5                | end
    move.l  8(a2),a6                | buf
    move.l  12(a2),a3               | the note
    tst.l   56(a2)
    beq     .Lwr_out
.Lwr_post:
    cmp.l   a5,a4
    bcc     .Lwr_out
    move.l  20(a4),d0               | lump
    beq     .Lwr_out
    tst.l   28(a4)                  | alttile
    bne     .Lwr_out
    add.l   d0,d0
    add.l   d0,d0
    move.l  20(a2),a0
    move.l  (a0,d0.l),a0            | s1: the lump's word copy
    cmp.w   #0,a0
    beq     .Lwr_out
    move.l  4(a4),d2                | wh
    ble     .Lwr_out
    cmp.l   #0xFFFF,d2
    bhi     .Lwr_out
    | the geometry, for (wh, maxheight + 32)
    cmp.l   60(a2),d2
    bne.s   1f
    move.l  64(a2),d0
    cmp.l   52(a2),d0
    beq.s   2f
1:  move.l  d2,60(a2)
    move.l  52(a2),64(a2)
    move.l  d2,d3
    lsl.l   #4,d3                   | ginv = wh << 4
    move.l  d3,68(a2)
    move.w  d2,d0
    mulu.w  50(a2),d0               | wh * pheight
    lsl.l   #4,d0
    addq.l  #1,d3
    lsr.l   #1,d3                   | (ginv + 1) >> 1
    add.l   d3,d0
    move.l  44(a2),d1
    sub.l   d0,d1                   | gtop
    move.l  d1,72(a2)
    move.w  d2,d0
    mulu.w  54(a2),d0               | wh * ht
    lsl.l   #4,d0
    add.l   d1,d0                   | gbot
    move.l  d0,76(a2)
2:  move.l  72(a2),d4
    add.l   #0xFFFF,d4
    swap    d4
    ext.l   d4                      | yl
    move.l  76(a2),d5
    subq.l  #1,d5
    swap    d5
    ext.l   d5
    addq.l  #1,d5                   | yh
    move.l  32(a2),d6               | viewheight
    cmp.l   d6,d4
    blt.s   3f
    move.l  d6,8(a4)                | ceilingclip
    subq.l  #1,d6
    move.l  d6,12(a4)               | floorclip
    bra     .Lwr_next
3:  tst.l   d4
    bpl.s   4f
    moveq   #0,d4
4:  | the texture's step for wh
    cmp.l   80(a2),d2
    beq.s   6f
    move.l  d2,80(a2)
    cmp.l   #0x1000,d2
    bls.s   5f
    move.l  #0x10000000,d0
    divu.w  d2,d0
    and.l   #0xFFFF,d0
    move.l  d0,84(a2)
    bra.s   6f
5:  move.l  #0x1000,d0              | 2^28's high word, by two divu.w
    divu.w  d2,d0
    move.l  d0,d1
    swap    d1                      | the quotient up, the remainder below
    clr.w   d0                      | the remainder : 0
    divu.w  d2,d0
    move.w  d0,d1
    move.l  d1,84(a2)
6:  move.l  16(a4),d7
    lsr.l   #4,d7
    and.l   #0xFC0,d7
    add.l   d7,d7                   | the word copy's column
    add.l   d7,a0                   | its column
    tst.l   d5
    bpl.s   7f
    moveq   #-1,d0
    move.l  d0,12(a4)               | floorclip
    clr.l   8(a4)                   | ceilingclip
    bra     .Lwr_next
7:  cmp.l   d6,d5
    ble.s   8f
    move.l  d6,d5
8:  move.l  d4,8(a4)
    move.l  d5,d0
    subq.l  #1,d0
    move.l  d0,12(a4)
    sub.l   d4,d5                   | rows
    ble     .Lwr_next
    | frac = texmid + iscale * (yl - centery), as atari_mul32x16
    move.l  84(a2),d3               | iscale
    move.l  d4,d1
    sub.l   36(a2),d1               | b: under 16 bits
    move.w  d3,d0
    mulu.w  d1,d0                   | a's low word * b's
    move.l  d3,d2
    swap    d2
    tst.w   d2
    beq.s   9f
    mulu.w  d1,d2
    swap    d2
    clr.w   d2
    add.l   d2,d0
9:  tst.w   d1
    bpl.s   10f
    move.l  d3,d2
    swap    d2
    clr.w   d2
    sub.l   d2,d0                   | b negative: 65536 a less
10: add.l   40(a2),d0               | frac
    add.l   d4,d4
    add.l   d4,d4
    lea     _ylookup,a1
    move.l  (a1,d4.l),d4
    add.l   a6,d4                   | dest
    move.l  d4,(a3)+
    move.l  a0,(a3)+
    clr.l   (a3)+
    move.l  d0,(a3)+
    move.l  d3,(a3)+
    move.w  d5,(a3)+
    move.w  30(a2),(a3)+            | twice
    cmp.l   #2,24(a2)
    bne.s   11f
    tst.l   28(a2)
    bne.s   11f
    addq.l  #1,d4                   | the pair's other column
    move.l  d4,(a3)+
    move.l  a0,(a3)+
    clr.l   (a3)+
    move.l  d0,(a3)+
    move.l  d3,(a3)+
    move.w  d5,(a3)+
    clr.w   (a3)+
11: cmp.l   16(a2),a3
    bcs.s   .Lwr_next
    move.l  a3,_atari_wallcol_next
    jsr     _ATARI_WallColumnsFlush
    move.l  _atari_wallcol_next,a3
.Lwr_next:
    move.l  24(a2),d0               | step
    cmp.l   #2,d0
    bne.s   12f
    move.l  8(a4),40(a4)            | the next post's clips: this one's
    move.l  12(a4),44(a4)
12: add.l   d0,a6
    lsl.l   #5,d0                   | 32 bytes a post
    add.l   d0,a4
    bra     .Lwr_post
.Lwr_out:
    move.l  a4,(a2)
    move.l  a6,8(a2)
    move.l  a3,12(a2)
    move.l  a4,d0
    movem.l (sp)+,d2-d7/a2-a6
    rts
