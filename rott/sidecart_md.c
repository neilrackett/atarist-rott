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
 * included) rather than single commands. A TT's or a Falcon's 68030 has a
 * data cache in the same way (and a 68040 or 68060): the same brackets
 * turn it off, through CACR.
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
unsigned short md_command_retries;

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

#ifndef C__CPU
#define C__CPU 0x5f435055L /* '_CPU' */
#endif

/* 0 (68000), 10, 20, 30, 40, 60. */
static int md_cpu(void) {
  static int cpu = -1;
  long v = 0;
  if (cpu < 0) cpu = Getcookie(C__CPU, &v) == C_FOUND ? (int)v : 0;
  return cpu;
}

/* ── Supervisor ──────────────────────────────────────────────────────── */
/* Super(0L)/Super(ssp) is only right when called from user mode; ROTT is
 * in user mode but some callers may not be (ATARI_MD_FinishUpdate runs
 * most of a frame in supervisor mode: sidecart_md_super_begin). Only a
 * machine with a cache to guard needs supervisor here. A Mega STE is a
 * 68000, where MOVE from SR is allowed in user mode: so no SUP_INQUIRE
 * trap to find out. From the 68010 on it is privileged (and TOS's stand-in
 * for it there reads the condition codes alone), so those ask GEMDOS. */
#define MD_ALREADY_SUPER (-1L)

static long md_super_enter(void) {
  unsigned short sr;
  if (md_cpu() >= 10) {
    if (Super(1L)) return MD_ALREADY_SUPER;
    return (long)Super(0L);
  }
  __asm__ volatile("move.w %%sr,%0" : "=d"(sr));
  if (sr & 0x2000) return MD_ALREADY_SUPER;
  return (long)Super(0L);
}

static void md_super_exit(long token) {
  if (token != MD_ALREADY_SUPER) Super((void *)token);
}

/* ── Cache guard ─────────────────────────────────────────────────────── */
/* A read the cache answers never reaches the cartridge: a stale frame, or
 * a command's token that never seems to come. A Mega STE's cache goes off
 * at $FFFF8E21. A 68030's data cache goes off and empty through CACR (ED
 * clear, CD set), its instruction cache left alone, as md-xt and md-doom
 * do it; TOS 3.06's PMMU tables make the cartridge cache-inhibited on a
 * TT, but nothing promises that of a Falcon or another TOS. A 68040's or
 * 68060's has its dirty lines written out first (CPUSHA). All in
 * supervisor mode. */

static int md_is_megaste(void) {
  static int cached = -1;
  long mch = 0;
  if (cached < 0) {
    cached = (Getcookie(C__MCH, &mch) == C_FOUND && mch == MCH_MEGA_STE_COOKIE);
  }
  return cached;
}

#define MD_GUARD_NONE 0
#define MD_GUARD_MSTE 1
#define MD_GUARD_030 2
#define MD_GUARD_040 3

static int md_guard(void) {
  static int kind = -1;
  if (kind < 0) {
    const int cpu = md_cpu();
    kind = md_is_megaste() ? MD_GUARD_MSTE
           : cpu >= 40     ? MD_GUARD_040
           : cpu == 30     ? MD_GUARD_030
                           : MD_GUARD_NONE;
  }
  return kind;
}

#define CACR_030_ED 0x00000100UL  /* data cache on */
#define CACR_030_CD 0x00000800UL  /* empty it (reads back 0) */
#define CACR_040_EDC 0x80000000UL /* data cache on */

/* CACR = (CACR & keep) | set, the old one back; and CACR = value. With
 * dirty, a 68040's data cache is written out and emptied first. Opcodes by
 * hand: the game is assembled for the 68000. */
unsigned long md_cacr_swap(unsigned long keep, unsigned long set);
void md_cacr_put(unsigned long value);
unsigned long md_cacr_swap040(unsigned long keep);
__asm__(
    "    .text\n"
    "    .even\n"
    "    .globl _md_cacr_swap\n"
    "_md_cacr_swap:\n"
    "    .word   0x4e7a,0x0002\n" /* movec cacr,d0 */
    "    move.l  d0,d1\n"
    "    and.l   4(sp),d1\n"
    "    or.l    8(sp),d1\n"
    "    .word   0x4e7b,0x1002\n" /* movec d1,cacr */
    "    rts\n"
    "    .globl _md_cacr_put\n"
    "_md_cacr_put:\n"
    "    move.l  4(sp),d0\n"
    "    .word   0x4e7b,0x0002\n" /* movec d0,cacr */
    "    rts\n"
    "    .globl _md_cacr_swap040\n"
    "_md_cacr_swap040:\n"
    "    .word   0xf478\n" /* cpusha dc */
    "    .word   0x4e7a,0x0002\n"
    "    move.l  d0,d1\n"
    "    and.l   4(sp),d1\n"
    "    .word   0x4e7b,0x1002\n"
    "    rts\n");

static int md_bus_depth;
static int md_bus_cache_off;
static unsigned char md_bus_saved_ctrl;
static unsigned long md_bus_saved_cacr;

/* Supervisor: the cache off if it is on, 1 if it was (its old state in
 * *saved, for md_cache_back). The vertical blank reads the cartridge too
 * (ATARI_MD_MusicVbl) and uses these as they are. */
int md_cache_off(unsigned long *saved) {
  switch (md_guard()) {
    case MD_GUARD_MSTE: {
      const unsigned char ctl = *MEGASTE_CTRL_ADDR;
      *saved = ctl;
      if (!(ctl & MEGASTE_CTRL_CACHE_BIT)) return 0;
      *MEGASTE_CTRL_ADDR = (unsigned char)(ctl & ~MEGASTE_CTRL_CACHE_BIT);
      return 1;
    }
    case MD_GUARD_030:
      *saved = md_cacr_swap(~CACR_030_ED, CACR_030_CD);
      return 1;
    case MD_GUARD_040:
      *saved = md_cacr_swap040(~CACR_040_EDC);
      return 1;
    default:
      return 0;
  }
}

void md_cache_back(unsigned long saved) {
  switch (md_guard()) {
    case MD_GUARD_MSTE:
      *MEGASTE_CTRL_ADDR = (unsigned char)saved;
      break;
    case MD_GUARD_030:
      md_cacr_put(saved | CACR_030_CD);
      break;
    case MD_GUARD_040:
      md_cacr_put(saved); /* nothing went into it meanwhile */
      break;
    default:
      break;
  }
}

void sidecart_md_bus_begin(void) {
  unsigned long saved = 0;
  long ssp;
  if (md_bus_depth++ || !md_guard()) return;
  ssp = md_super_enter();
  md_bus_cache_off = md_cache_off(&saved);
  md_bus_saved_ctrl = (unsigned char)saved;
  md_bus_saved_cacr = saved;
  md_super_exit(ssp);
}

void sidecart_md_bus_end(void) {
  long ssp;
  if (md_bus_depth <= 0 || --md_bus_depth) return;
  if (!md_bus_cache_off) return;
  ssp = md_super_enter();
  md_cache_back(md_guard() == MD_GUARD_MSTE ? md_bus_saved_ctrl
                                            : md_bus_saved_cacr);
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
  if (!md_guard()) return MD_ALREADY_SUPER;
  return md_super_enter();
}

long sidecart_md_super_force(void) { return md_super_enter(); }

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
    if (attempt) md_command_retries++;
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
    if (attempt) md_command_retries++;
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
