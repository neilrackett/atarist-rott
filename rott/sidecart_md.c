/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
/*
 * sidecart_md.c - SidecarTridge Multi-device client (MD/ROTT).
 *
 * From atarist-stdoom's linuxdoom-1.10/sidecart_md.c: the mode-aware
 * supervisor handling, the Mega STE cache guard (canonical MSTE_CC values,
 * toggling only bit 0), the one-off settle delay and the optional
 * interrupt mask are stdoom's, hardware-proven there. What changed: the
 * command ids and ROM4 layout come from rott_md_protocol.h, commands retry,
 * and the cache guard brackets whole stretches of traffic (reads of ROM4
 * included) rather than single commands.
 */

#if defined(__MINT__)

#include "sidecart_md.h"

#include <mint/cookie.h>
#include <mint/osbind.h>

/* sidecart_stubs.S */
extern int md_send_sync_command(int cmd, int payload_size, long d3, long d4);
extern int md_send_sync_write_command(int cmd, const char *buf,
                                      int byte_count, long d3, long d4,
                                      long d5);

long md_command_timeout = MD_TIMEOUT_DETECT;
unsigned short md_command_failures;

#define MD_RETRIES 3

#ifndef C__MCH
#define C__MCH 0x5f4d4348L /* '_MCH' */
#endif
#ifndef C_FOUND
#define C_FOUND 0
#endif
#define MCH_MEGA_STE_COOKIE 0x00010010L
#define MEGASTE_CTRL_ADDR ((volatile unsigned char *)0xFFFF8E21UL)
#define MEGASTE_CTRL_CACHE_BIT 0x01u

/* ── Supervisor ──────────────────────────────────────────────────────── */
/* Super(0L)/Super(ssp) is only right when called from user mode; ROTT is
 * in user mode but some callers may not be (ATARI_MD_FinishUpdate runs
 * most of a frame in supervisor mode: sidecart_md_super_begin). Only the
 * Mega STE needs supervisor here, and it is a 68000, where MOVE from SR is
 * allowed in user mode: so no SUP_INQUIRE trap to find out. */
#define MD_ALREADY_SUPER (-1L)

static long md_super_enter_mste(void) {
  unsigned short sr;
  __asm__ volatile("move.w %%sr,%0" : "=d"(sr));
  if (sr & 0x2000) return MD_ALREADY_SUPER;
  return (long)Super(0L);
}

static void md_super_exit(long token) {
  if (token != MD_ALREADY_SUPER) Super((void *)token);
}

/* ── Mega STE cache guard ────────────────────────────────────────────── */

static int md_is_megaste(void) {
  static int cached = -1;
  long mch = 0;
  if (cached < 0) {
    cached = (Getcookie(C__MCH, &mch) == C_FOUND && mch == MCH_MEGA_STE_COOKIE);
  }
  return cached;
}

static int md_bus_depth;
static int md_bus_cache_off;
static unsigned char md_bus_saved_ctrl;

void sidecart_md_bus_begin(void) {
  long ssp;
  if (md_bus_depth++ || !md_is_megaste()) return;
  ssp = md_super_enter_mste();
  md_bus_saved_ctrl = *MEGASTE_CTRL_ADDR;
  md_bus_cache_off = (md_bus_saved_ctrl & MEGASTE_CTRL_CACHE_BIT) != 0;
  if (md_bus_cache_off) {
    *MEGASTE_CTRL_ADDR =
        (unsigned char)(md_bus_saved_ctrl & ~MEGASTE_CTRL_CACHE_BIT);
  }
  md_super_exit(ssp);
}

void sidecart_md_bus_end(void) {
  long ssp;
  if (md_bus_depth <= 0 || --md_bus_depth) return;
  if (!md_bus_cache_off) return;
  ssp = md_super_enter_mste();
  *MEGASTE_CTRL_ADDR = md_bus_saved_ctrl;
  md_super_exit(ssp);
  md_bus_cache_off = 0;
}

/* Back to user mode after sidecart_md_super_begin's Super(0L), on the
 * current stack. GEMDOS Super(ssp) would resume with the stack pointer the
 * Super(0L) left in USP, which is only right at the same stack depth (the
 * usual pattern, both in one function); this frame is not that one's. */
void md_user_mode(long ssp);
__asm__(
    "    .text\n"
    "    .even\n"
    "_md_user_mode:\n"
    "    move.l  (sp)+,a0\n" /* return address */
    "    move.l  (sp),d0\n"  /* ssp */
    "    move.l  sp,a1\n"
    "    move.l  a1,usp\n"
    "    move.l  d0,sp\n"
    "    andi.w  #0xdfff,sr\n"
    "    jmp     (a0)\n");

long sidecart_md_super_begin(void) {
  if (!md_is_megaste()) return MD_ALREADY_SUPER;
  return md_super_enter_mste();
}

long sidecart_md_super_force(void) { return md_super_enter_mste(); }

void sidecart_md_super_end(long token) {
  if (token != MD_ALREADY_SUPER) md_user_mode(token);
}

/* ── Interrupt mask ──────────────────────────────────────────────────── */

static int md_mask_intr;

void sidecart_md_set_intr_mask(int enable) { md_mask_intr = enable; }

static unsigned short md_intr_begin(void) {
  unsigned short saved = 0;
  if (md_mask_intr) {
    __asm__ volatile(
        "move.w %%sr,%0\n\t"
        "ori.w  #0x0700,%%sr"
        : "=d"(saved)
        :
        : "cc");
  }
  return saved;
}

static void md_intr_end(unsigned short saved) {
  if (md_mask_intr) {
    __asm__ volatile("move.w %0,%%sr" : : "d"(saved) : "cc");
  }
}

/* ── Settle ──────────────────────────────────────────────────────────── */
/* From md-js via stdoom: a freshly booted cartridge can lose the first
 * command to a bus settle race, so wait once before the first. */
static void md_settle_once(void) {
  static int settled;
  volatile long i;
  if (settled) return;
  for (i = 0; i < 200000L; i++) {
  }
  settled = 1;
}

/* ── Commands ────────────────────────────────────────────────────────── */

int sidecart_md_present(void) {
  return *MD_ROM4(MD_READY_OFFSET) == MD_READY_MAGIC;
}

int sidecart_md_command(int cmd, long d3, long d4) {
  int attempt;
  int rc = -1;
  md_settle_once();
  sidecart_md_bus_begin();
  for (attempt = 0; attempt < MD_RETRIES && rc; attempt++) {
    unsigned short sr = md_intr_begin();
    rc = md_send_sync_command(cmd, 8, d3, d4);
    md_intr_end(sr);
  }
  sidecart_md_bus_end();
  if (rc) md_command_failures++;
  return rc;
}

int sidecart_md_write(int cmd, const void *buf, int bytes, long d3, long d4,
                      long d5) {
  int attempt;
  int rc = -1;
  md_settle_once();
  sidecart_md_bus_begin();
  for (attempt = 0; attempt < MD_RETRIES && rc; attempt++) {
    unsigned short sr = md_intr_begin();
    rc = md_send_sync_write_command(cmd, (const char *)buf, bytes, d3, d4, d5);
    md_intr_end(sr);
  }
  sidecart_md_bus_end();
  if (rc) md_command_failures++;
  return rc;
}

/* The RP stores the text so that byte reads see it in order. */
void sidecart_md_result(char *buf, int size) {
  volatile const unsigned char *src = MD_ROM4(MD_RESULT_OFFSET);
  int i;
  sidecart_md_bus_begin();
  for (i = 0; i < size - 1 && i < MD_RESULT_SIZE - 1; i++) {
    buf[i] = (char)src[i];
    if (!buf[i]) break;
  }
  buf[i] = 0;
  sidecart_md_bus_end();
}

#endif /* __MINT__ */
