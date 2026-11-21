/*
 * tools/bench/fixtest.c: atari_cpu.S's FixedScale and atari_mulshift64
 * against the C they stand for, on random and edge-case inputs, on the
 * 68000 paths and (on a 68020 or later) the 68030 ones. Built and run by
 * tools/bench/fixtest.sh; writes FIXTEST.TXT.
 */
#include <stdio.h>
#include <mint/osbind.h>
#include <mint/cookie.h>

typedef long fixed;
typedef long long i64;

extern unsigned char atari_cpu_l32, atari_cpu_l64;
fixed FixedScale(fixed a, fixed b, fixed c);
fixed atari_mulshift64(fixed a, fixed b, int shift);
fixed FixedMul(fixed a, fixed b);
#include "../../rott/atari_fixmul.h"

/* watcom.h's atari_fixed_mul16, which FixedMul gives on every machine */
static fixed mul16_c(fixed a, fixed b)
{
   int neg = 0;
   unsigned long ua, ub, lo, mid, hi;
   if (a < 0) { ua = (unsigned long)(-a); neg = 1; } else ua = (unsigned long)a;
   if (b < 0) { ub = (unsigned long)(-b); neg ^= 1; } else ub = (unsigned long)b;
   lo = (ua & 0xffff) * (ub & 0xffff) + 0x8000u;
   mid = (ua & 0xffff) * (ub >> 16) + (ua >> 16) * (ub & 0xffff);
   hi = (ua >> 16) * (ub >> 16);
   lo = (hi << 16) + mid + (lo >> 16);
   return neg ? -(fixed)lo : (fixed)lo;
}

/* watcom.c's, as the game links them */
fixed FixedScaleC(fixed orig, fixed factor, fixed divisor)
{
   i64 w = ((i64)orig * (i64)factor) / (i64)divisor;
   return (fixed)(w & 0xffffffff);
}

static fixed mulshift_c(fixed a, fixed b, int shift)
{
   i64 z = (i64)a * (i64)b;
   return (fixed)((((unsigned long long)z) >> shift) & 0xffffffff);
}

static unsigned long seed = 12345;
static unsigned long rnd(void)
{
   seed = seed * 1103515245UL + 12345UL;
   return (seed >> 16) | ((seed * 69069UL) & 0xffff0000UL);
}

static fixed val(void)
{
   static const unsigned long edge[] = {
      0, 1, 2, 0xffff, 0x10000, 0x10001, 0x7fff, 0x8000, 0xffffff, 0x1000000,
      0x7fffffff, 0x80000000UL, 0xffffffffUL, 0xfffffffeUL, 0xffff0000UL, 0x12345678 };
   unsigned long r = rnd();
   if ((r & 15) == 0)
      return (fixed)edge[(r >> 4) & 15];
   r = rnd() >> (rnd() & 31);
   return (rnd() & 1) ? -(fixed)r : (fixed)r;
}

static long bad, tests;
static unsigned char mode;

/* the references with libgcc's own arithmetic, whatever is being tested */
static fixed ref_scale(fixed a, fixed b, fixed c)
{
   fixed y;
   atari_cpu_l32 = atari_cpu_l64 = 0;
   y = FixedScaleC(a, b, c);
   atari_cpu_l32 = atari_cpu_l64 = mode;
   return y;
}

static fixed ref_mulshift(fixed a, fixed b, int s)
{
   fixed y;
   atari_cpu_l32 = atari_cpu_l64 = 0;
   y = mulshift_c(a, b, s);
   atari_cpu_l32 = atari_cpu_l64 = mode;
   return y;
}
static FILE *out;

static void run(const char *what, long n)
{
   long i;
   for (i = 0; i < n; i++)
   {
      fixed a = val(), b = val(), c = val();
      int s = (int)(rnd() & 63);
      if (c == 0)
         c = 1;
      /* the C's own answer for a quotient past 32 bits: still the C's */
      {
         fixed x = FixedScale(a, b, c), y = ref_scale(a, b, c);
         tests++;
         if (x != y && bad++ < 20)
            fprintf(out, "%s FixedScale(%08lx,%08lx,%08lx) %08lx != %08lx\n",
                    what, a, b, c, x, y);
      }
      {
         fixed z = atari_FixedMul(a, b);
         fixed x = FixedMul(a, b), y;
         if (z != x && bad++ < 60)
            fprintf(out, "%s atari_FixedMul(%08lx,%08lx) %08lx != %08lx\n", what, a, b, z, x);
         atari_cpu_l32 = atari_cpu_l64 = 0;
         y = mul16_c(a, b);
         atari_cpu_l32 = atari_cpu_l64 = mode;
         tests++;
         if (x != y && bad++ < 60)
            fprintf(out, "%s FixedMul(%08lx,%08lx) %08lx != %08lx\n", what, a, b, x, y);
      }
      if (s == 16)
         s = 15;
      {
         fixed x = atari_mulshift64(a, b, s), y = ref_mulshift(a, b, s);
         tests++;
         if (x != y && bad++ < 40)
            fprintf(out, "%s mulshift(%08lx,%08lx,%d) %08lx != %08lx\n",
                    what, a, b, s, x, y);
      }
   }
}

int main(void)
{
   long cpu = 0;
   const long n = 20000;

   out = fopen("FIXTEST.TXT", "w");
   if (!out)
      return 1;
   Getcookie(C__CPU, &cpu);
   mode = 0;
   atari_cpu_l32 = atari_cpu_l64 = 0;
   run("68000", n);
   if (cpu >= 20 && cpu < 60)
   {
      mode = 1;
      atari_cpu_l32 = atari_cpu_l64 = 1;
      run("68030", n);
   }
   fprintf(out, "cpu %ld: %ld tests, %ld wrong\n", cpu, tests, bad);
   fclose(out);
   return 0;
}
