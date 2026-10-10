/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdint.h>
#include <mint/osbind.h>
#include <mint/cookie.h>

#if ATARI_SDL
/*
 * SDL on Atari can use stack-heavy setup paths (alloca in video backend).
 * Reserve a larger process stack so video mode setup succeeds reliably.
 */
long _stksize = 256L * 1024L;
#endif

#define MCH_MEGA_STE 0x00010010L
/* $FFFF8E21: bit 0 the cache, bit 1 the 16MHz clock. Only those two bits
 * are changed, as STDL's video.c does (hardware-tested,
 * https://github.com/neilrackett/atarist-stdl): the rest of the byte is
 * not ours. Atari's control panel and the MSTE_CC utilities keep the high
 * bits set ($FF, $FE, $F4), where a bare $03 cleared them, bit 3 among
 * them; Hatari only looks at bits 0 and 1, so it can't tell. The cache is
 * nearly all of the speedup: the bus stays at 8MHz either way. */
#define MSTE_CTL (*(volatile uint8_t *)0xFFFF8E21UL)

/* For the Help key's overlay: $FFFF8E21 as TOS left it and once set (-1
 * until then, and on anything but a Mega STE). */
int megaste_ctl_boot = -1;
int megaste_ctl_set = -1;

static long megaste_enable_16mhz_cache_super(void)
{
  if (megaste_ctl_boot < 0)
    megaste_ctl_boot = MSTE_CTL;
  MSTE_CTL = (uint8_t)(MSTE_CTL | 0x03u); /* Mega STE 16MHz with cache */
  megaste_ctl_set = MSTE_CTL;
  return 0;
}

#if defined(ATARI_NATIVE)
/* How fast the CPU really is, for the overlay: passes of a short loop in
 * 10 ticks of the 200Hz count (50ms), as STDL's spdprob.c times its loop
 * against it. The cache is most of a Mega STE's 16MHz, so this roughly
 * doubles from 8MHz (or 16MHz without it) to 16MHz with it. */
unsigned int cpu_speed_passes;

static long cpu_speed_super(void)
{
  volatile const uint32_t *hz200 = (volatile const uint32_t *)0x4BAUL;
  uint32_t t0 = *hz200;
  unsigned int n = 0;

  while (*hz200 == t0) {
  } /* from a tick's start */
  t0 = *hz200;
  while (*hz200 - t0 < 10)
    n++;
  cpu_speed_passes = n;
  return 0;
}

void cpu_speed_measure(void)
{
  if (cpu_speed_passes == 0)
    Supexec(cpu_speed_super);
}
#endif

int is_megaste(void)
{
  long mch = 0;
  if (C_FOUND != Getcookie(C__MCH, &mch))
    return 0;
  return mch == MCH_MEGA_STE;
}

void megaste_enable_16mhz_cache(void)
{
  Supexec(megaste_enable_16mhz_cache_super);
}
