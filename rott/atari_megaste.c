#include <stdint.h>
#include <mint/osbind.h>
#include <mint/cookie.h>

#if defined(__MINT__)
/*
 * SDL on Atari can use stack-heavy setup paths (alloca in video backend).
 * Reserve a larger process stack so video mode setup succeeds reliably.
 */
long _stksize = 256L * 1024L;
#endif

#define MCH_MEGA_STE 0x00010010L
static long megaste_enable_16mhz_cache_super(void)
{
  *(volatile uint8_t *)0xFFFF8E21 = 0x03; /* Mega STE 16MHz with cache */
  return 0;
}

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
