/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * sidecart_stubs.S — GAS-syntax wrappers for the SidecarTridge protocol.
 * From atarist-stdoom (linuxdoom-1.10/sidecart_stubs.S), itself from md-js.
 * MD/ROTT change: the token-wait timeout is the C variable
 * _md_command_timeout rather than a constant, so the client can wait longer
 * while the firmware is busy and give up quickly otherwise.
 *
 * Provides send_sync_command_to_sidecart and send_sync_write_command_to_sidecart
 * as linkable symbols for C code (sidecart_md.c).
 *
 * Translated from src/inc/sidecart_functions.s with COMMAND_SYNC_USE_DSKBUF=0
 * (all ifne/endif blocks for disk-buffer or stack copy modes are inactive and
 * omitted here).
 *
 * Register ABI (matches sidecart_functions.s):
 *
 *   send_sync_command_to_sidecart:
 *     d0.w  = command code
 *     d1.w  = payload size (bytes, EXCLUDING the 4-byte token — added internally)
 *     d2.l  = token seed (loaded from RANDOM_TOKEN_SEED_ADDR internally)
 *     d3.l  = payload word 0 (if d1 >= 6)
 *     d4.l  = payload word 1 (if d1 >= 10)
 *     d5.l  = payload word 2 (if d1 >= 14)
 *     d6.l  = payload word 3 (if d1 >= 18)
 *     returns d0 = 0 (success) or -1 (timeout)
 *
 *   send_sync_write_command_to_sidecart:
 *     d0.w  = command code
 *     d3.l  = header word 0
 *     d4.l  = header word 1
 *     d5.l  = header word 2
 *     d6.w  = number of bytes to write from a4
 *     a4    = pointer to data buffer
 *     returns d0 = 0 (success) or -1 (timeout)
 */

/* ── Constants (must match main.s / sidecart_functions.s) ─────────────────── */
#define RANDOM_TOKEN_ADDR       0xFAF000
#define RANDOM_TOKEN_SEED_ADDR  (RANDOM_TOKEN_ADDR + 4)
#define ROMCMD_START_ADDR       0xFB0000
#define CMD_MAGIC_NUMBER        0xABCD
/* The token-wait loop count comes from _md_command_timeout (sidecart_md.c). */

    .text
    .even

/* ── send_sync_command_to_sidecart ─────────────────────────────────────────── */
    .globl send_sync_command_to_sidecart
send_sync_command_to_sidecart:
    | Load random token seed into d2
    move.l  RANDOM_TOKEN_SEED_ADDR,d2

    | Add 4 bytes for the token to the payload size
    addq.w  #4,d1

    | a1 = address of the token slot (written by RP2040 to unblock us)
    lea     RANDOM_TOKEN_ADDR,a1

    | a0 = ROM3 mid-point used for bus signalling: tst.b (a0, dN.w)
    move.l  #(ROMCMD_START_ADDR + 0x8000),a0

    | Send magic number header
    move.w  #CMD_MAGIC_NUMBER,d7
    tst.b   (a0,d7.w)

    | Checksum accumulator in d7
    clr.l   d7

    | Send command code
    add.w   d0,d7
    tst.b   (a0,d0.w)

    | Send payload size
    add.w   d1,d7
    tst.b   (a0,d1.w)
    tst.w   d1
    beq     .Lno_more_payload

    | Send token low word (d2.w)
    add.w   d2,d7
    tst.b   (a0,d2.w)
    cmp.w   #2,d1
    beq     .Lno_more_payload

    | Send token high word (swap d2)
    swap    d2
    add.w   d2,d7
    tst.b   (a0,d2.w)
    cmp.w   #4,d1
    beq     .Lno_more_payload

    | d3 low
    add.w   d3,d7
    tst.b   (a0,d3.w)
    cmp.w   #6,d1
    beq     .Lno_more_payload

    | d3 high
    swap    d3
    add.w   d3,d7
    tst.b   (a0,d3.w)
    cmp.w   #8,d1
    beq     .Lno_more_payload

    | d4 low
    add.w   d4,d7
    tst.b   (a0,d4.w)
    cmp.w   #10,d1
    beq     .Lno_more_payload

    | d4 high
    swap    d4
    add.w   d4,d7
    tst.b   (a0,d4.w)
    cmp.w   #12,d1
    beq.s   .Lno_more_payload

    | d5 low
    add.w   d5,d7
    tst.b   (a0,d5.w)
    cmp.w   #14,d1
    beq.s   .Lno_more_payload

    | d5 high
    swap    d5
    add.w   d5,d7
    tst.b   (a0,d5.w)
    cmp.w   #16,d1
    beq.s   .Lno_more_payload

    | d6 low
    add.w   d6,d7
    tst.b   (a0,d6.w)
    cmp.w   #18,d1
    beq.s   .Lno_more_payload

    | d6 high
    swap    d6
    add.w   d6,d7
    tst.b   (a0,d6.w)

.Lno_more_payload:
    | Send checksum
    tst.b   (a0,d7.w)

    | Wait for token: RP2040 writes d2 (token seed) back to RANDOM_TOKEN_ADDR
    swap    d2                          | restore d2 to original seed value
    move.l  _md_command_timeout,d7
    moveq   #0,d0                       | assume success
.Lwait_token:
    cmp.l   (a1),d2
    bne.s   .Ltoken_not_ready
    cmp.l   RANDOM_TOKEN_SEED_ADDR,d2
    bne.s   .Ltoken_found
.Ltoken_not_ready:
    subq.l  #1,d7
    bne.s   .Lwait_token
    subq.l  #1,d0                       | timeout: d0 = -1
.Ltoken_found:
    rts

/* ── C ABI wrappers (avoid fragile inline asm constraints) ─────────────── */
/* int md_send_sync_command(int cmd, int payload_size, long d3, long d4)
 *
 * m68k-atari-mint-gcc uses 32-bit ints by default. Each arg occupies 4 bytes
 * on the stack; for 16-bit values the LOW 16 bits sit at offset +2 of the
 * 4-byte slot (big-endian). Read full longwords and let callee use d0.w/d1.w.
 */
    .globl _md_send_sync_command
_md_send_sync_command:
    /* Preserve callee-saved registers expected by the C ABI (11 regs = 44 bytes)
     * + 4 bytes return address = 48 bytes to the first arg. */
    movem.l d2-d7/a2-a6,-(sp)
    move.l  48(sp),d0           | cmd (low 16 bits used)
    move.l  52(sp),d1           | payload_size (low 16 bits used)
    move.l  56(sp),d3           | d3 (full longword)
    move.l  60(sp),d4           | d4 (full longword)
    jsr     send_sync_command_to_sidecart
    movem.l (sp)+,d2-d7/a2-a6
    rts


/* ── send_sync_write_command_to_sidecart ───────────────────────────────────── */
    .globl send_sync_write_command_to_sidecart
send_sync_write_command_to_sidecart:
    | Load random token seed
    move.l  RANDOM_TOKEN_SEED_ADDR,d2

    | a1 = token slot, a0 = ROM3 mid-point
    lea     RANDOM_TOKEN_ADDR,a1
    move.l  #(ROMCMD_START_ADDR + 0x8000),a0

    | Compute total payload size:
    |   16 bytes fixed header (token + d3 + d4 + d5) + buffer bytes, rounded up to word
    and.l   #0xFFFF,d6          | clamp buffer size to 16 bits
    moveq.l #16,d1
    add.l   d6,d1
    addq.l  #1,d1               | round up
    lsr.l   #1,d1
    lsl.l   #1,d1               | now d1 = word-aligned payload size

    | Send magic number
    move.w  #CMD_MAGIC_NUMBER,d7
    tst.b   (a0,d7.w)

    clr.l   d7                  | checksum

    | Command code
    add.w   d0,d7
    tst.b   (a0,d0.w)

    | Payload size
    add.w   d1,d7
    tst.b   (a0,d1.w)

    | Token low (d2.w)
    add.w   d2,d7
    tst.b   (a0,d2.w)

    | Token high
    swap    d2
    add.w   d2,d7
    tst.b   (a0,d2.w)

    | d3 low
    add.w   d3,d7
    tst.b   (a0,d3.w)

    | d3 high
    swap    d3
    add.w   d3,d7
    tst.b   (a0,d3.w)

    | d4 low
    add.w   d4,d7
    tst.b   (a0,d4.w)

    | d4 high
    swap    d4
    add.w   d4,d7
    tst.b   (a0,d4.w)

    | d5 low
    add.w   d5,d7
    tst.b   (a0,d5.w)

    | d5 high
    swap    d5
    add.w   d5,d7
    tst.b   (a0,d5.w)

    | Stream buffer bytes from a4, two at a time
    | d5 = byte count, d6 = running checksum accumulator (carry the old d7 in)
    tst.l   d6
    beq     .Lbuf_done
    move.l  d6,d5               | d5 = byte count
    move.l  d7,d6               | d6 = checksum so far
    clr.l   d7

    btst    #0,d5
    bne.s   .Lbyteodd

    | Even byte count
    addq.l  #1,d5               | round up
    lsr.w   #1,d5               | word pairs
    subq.w  #1,d5               | dbf pre-decrement

    move.l  a4,d0
    btst    #0,d0
    beq.s   .Leven_loop

.Lodd_loop:
    move.b  (a4)+,d3
    lsl.w   #8,d3
    move.b  (a4)+,d3
    tst.b   (a0,d3.w)
    add.w   d3,d7
    dbf     d5,.Lodd_loop
    bra.s   .Lbuf_done

.Leven_loop:
    move.w  (a4)+,d0
    add.w   d0,d7
    tst.b   (a0,d0.w)
    dbf     d5,.Leven_loop
    bra.s   .Lbuf_done

.Lbyteodd:
    | Odd byte count
    addq.l  #1,d5
    lsr.w   #1,d5
    move.l  a4,d0
    btst    #0,d0
    beq.s   .Lbyteodd_odd_head

    subq.w  #1,d5
    beq.s   .Lbyteodd_tail
    subq.w  #1,d5
.Lbyteodd_even_copy:
    move.b  (a4)+,d3
    lsl.w   #8,d3
    move.b  (a4)+,d3
    tst.b   (a0,d3.w)
    add.w   d3,d7
    dbf     d5,.Lbyteodd_even_copy
.Lbyteodd_tail:
    move.b  (a4)+,d0
    lsl.w   #8,d0
    and.w   #0xFF00,d0
    add.w   d0,d7
    tst.b   (a0,d0.w)
    bra.s   .Lbuf_done

.Lbyteodd_odd_head:
    subq.w  #1,d5
    beq.s   .Lbyteodd_odd_tail
    subq.w  #1,d5
.Lbyteodd_odd_copy:
    move.w  (a4)+,d0
    add.w   d0,d7
    tst.b   (a0,d0.w)
    dbf     d5,.Lbyteodd_odd_copy
.Lbyteodd_odd_tail:
    move.w  (a4)+,d0
    and.w   #0xFF00,d0
    add.w   d0,d7
    tst.b   (a0,d0.w)

.Lbuf_done:
    | Send checksum (buffer checksum + header checksum)
    add.w   d7,d6
    tst.b   (a0,d6.w)

    | Wait for token
    swap    d2
    move.l  _md_command_timeout,d6
    moveq   #0,d0
.Lwait_write_token:
    cmp.l   (a1),d2
    bne.s   .Lwrite_token_not_ready
    cmp.l   RANDOM_TOKEN_SEED_ADDR,d2
    bne.s   .Lwrite_token_found
.Lwrite_token_not_ready:
    subq.l  #1,d6
    bne.s   .Lwait_write_token
    subq.l  #1,d0
.Lwrite_token_found:
    rts

/* int md_send_sync_write_command(int cmd, const char *buf, int byte_count,
 *                                long d3, long d4, long d5)
 *
 * 32-bit ints — each int arg occupies a 4-byte slot; read full longwords. */
    .globl _md_send_sync_write_command
_md_send_sync_write_command:
    movem.l d2-d7/a2-a6,-(sp)
    move.l  48(sp),d0           | cmd (low 16 bits used)
    move.l  52(sp),a4           | buf pointer (32-bit)
    move.l  56(sp),d6           | byte_count (low 16 bits used)
    move.l  60(sp),d3           | chunk_idx
    move.l  64(sp),d4           | total_chunks
    move.l  68(sp),d5           | chunk_size
    jsr     send_sync_write_command_to_sidecart
    movem.l (sp)+,d2-d7/a2-a6
    rts
