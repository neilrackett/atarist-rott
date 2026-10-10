/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "rt_def.h"

#if !defined (AMIGA)
#include "watcom.h"

/* 
  C versions of watcom.h assembly.
  Uses the '__int64' type (see rt_def.h).
 */

/*
 * On ATARI_NATIVE builds the body is the inlined atari_fixed_mul16() from
 * watcom.h, which uses hardware mulu.w instead of software __mulsi3. Other
 * targets keep the 64-bit version.
 */
#if defined(ATARI_NATIVE)
fixed FixedMul(fixed a, fixed b)
{
	return atari_fixed_mul16(a, b);
}
#else
fixed FixedMul(fixed a, fixed b)
{
	__int64 scratch1 = (__int64) a * (__int64) b + (__int64) 0x8000;
	return (scratch1 >> 16) & 0xffffffff;
}
#endif

fixed FixedMulShift(fixed a, fixed b, fixed shift)
{
#if defined(ATARI_NATIVE)
	if (shift == 16)
		return atari_fixed_mul16(a, b);
#endif
	__int64 x = a;
	__int64 y = b;
	__int64 z = x * y;
	
	return (((unsigned __int64)z) >> shift) & 0xffffffff;
}

fixed FixedDiv2(fixed a, fixed b)
{
#if defined(ATARI_NATIVE)
	/* (a << 16) / b exactly as below, without the 64-bit divide: that
	 * was __divdi3 and its helpers, about 3000 cycles a call, and
	 * atan2_appx calls this. On the magnitudes, rounded toward zero as C
	 * does: the whole part, then sixteen bits below the point; one divu.w
	 * when |a| < |b| < 65536, as STDL's STDL_FixDiv does. The result is
	 * the quotient's low 32 bits, as the & below takes. Zero and the
	 * most negative int stay on the 64-bit path. */
	if (b != 0 && a != (fixed)0x80000000 && b != (fixed)0x80000000)
	{
		unsigned int ua = (a < 0) ? (unsigned int)-a : (unsigned int)a;
		unsigned int ub = (b < 0) ? (unsigned int)-b : (unsigned int)b;
		unsigned int q, r;
		int i;

		if (ua < ub && ub <= 0xFFFFu)
		{
			q = ua << 16; /* under ub << 16: the quotient fits a word */
			__asm__("divu.w %1,%0" : "+d"(q) : "d"((unsigned short)ub));
			q &= 0xFFFFu;
		}
		else
		{
			if (ua < ub)
			{
				q = 0;
				r = ua;
			}
			else
			{
				q = ua / ub;
				r = ua - q * ub;
			}
			/* r < ub < 2^31, so r << 1 never overflows */
			for (i = 0; i < 16; i++)
			{
				r <<= 1;
				q <<= 1;
				if (r >= ub)
				{
					r -= ub;
					q |= 1;
				}
			}
		}
		return ((a ^ b) < 0) ? (fixed)(0u - q) : (fixed)q;
	}
#endif
	__int64 x = (signed int)a;
	__int64 y = (signed int)b;
	__int64 z = x * 65536 / y;
	
	return (z) & 0xffffffff;
}

fixed FixedScale(fixed orig, fixed factor, fixed divisor)
{
	__int64 x = orig;
	__int64 y = factor;
	__int64 z = divisor;
	
	__int64 w = (x * y) / z;
	
	return (w) & 0xffffffff;
}
#else

typedef int fixed_t;

#ifndef C_FIXED_MATH
__inline__ fixed_t FixedMul040(fixed_t eins,fixed_t zwei)
{
    __asm __volatile
	("muls.l %1,%1:%0 \n\t"
	 "move %1,%0 \n\t"
	 "swap %0 "
					 
	  : "=d" (eins), "=d" (zwei)
	  : "0" (eins), "1" (zwei)
	);

    return eins;
}

__inline__ fixed_t FixedMul060(fixed_t eins,fixed_t zwei) 
{
	__asm __volatile
	("fmove.l	%0,fp0 \n\t"
	 "fmul.l	%2,fp0 \n\t"
	 "fmul.x	fp7,fp0 \n\t"

/*	 "fintrz.x	fp0,fp0 \n\t"*/
	 "fmove.l	fp0,%0"
					 
	  : "=d" (eins)
	  : "0" (eins), "d" (zwei)
	  : "fp0"
	);

	return eins;
}
 
__inline__ fixed_t FixedDiv040(fixed_t eins,fixed_t zwei)
{
	__asm __volatile


	("move.l	%0,d3\n\t"
	 "swap      %0\n\t"
	 "move.w    %0,d2\n\t"
	 "ext.l		d2\n\t"
	 "clr.w		%0\n\t"
	 "tst.l		%1\n\t"
	 "jeq		3f\n\t"
	 "divs.l	%1,d2:%0\n\t"
	 "jvc		1f\n"

	 "3: eor.l %1,d3\n\t"
	 "jmi       2f\n\t"
	 "move.l	#0x7FFFFFFF,%0\n\t"
	 "jra		1f\n"

	 "2: move.l #0x80000000,%0\n"
	 "1:\n"
	 
	 : "=d" (eins), "=d" (zwei)
	 : "0" (eins), "1" (zwei)
	 : "d2","d3"
	);
	
	return eins;
}

__inline fixed_t FixedDiv060(fixed_t eins,fixed_t zwei)
{
	__asm __volatile
	("tst.l		%1\n\t"
	 "jne		1f\n\t"

	 "eor.l		 %1,%0\n\t"
	 "jmi       2f\n\t"
	 "move.l	#0x7FFFFFFF,%0\n\t"
	 "jra		9f\n"

	 "2: move.l #0x80000000,%0\n\t"
     "jra		9f\n"
     
	 "1: fmove.l %0,fp0 \n\t"
	 "fdiv.l	%2,fp0 \n\t"
	 "fmul.x		fp6,fp0 \n\t"
/*	 "fintrz.x  fp0\n\t"*/
	 "fmove.l	fp0,%0\n"

	 "9:\n"
	 
	 : "=d" (eins)
	 : "0" (eins), "d" (zwei)
	 : "fp0"
	);

	return eins;
}

__inline int LongDiv(int eins,int zwei)
{
	__asm __volatile
	(
		"divsl.l %2,%0:%0\n\t"
		
		: "=d" (eins)
		: "0" (eins), "d" (zwei)
	);

	return eins;
}

__inline int ULongDiv(int eins,int zwei)
{
	__asm __volatile
	(
		"divul.l %2,%0:%0\n\t"
		
		: "=d" (eins)
		: "0" (eins), "d" (zwei)
	);

	return eins;
}
#endif

#endif
