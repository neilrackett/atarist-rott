/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
/*
Copyright (C) 1994-1995 Apogee Software, Ltd.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
#ifndef _watcom_h_public
#define _watcom_h_public

#if defined(__MINT__)
/*
 * 16.16 fixed-point multiply for the 68000 (which has hardware mulu.w/muls.w
 * but no 32x32 muls.l). Defined inline so the ~228 call sites avoid the
 * JSR/RTS + argument marshalling of an out-of-line helper.
 *
 * Multiplies the absolute values so all four 16x16 partial products are
 * unsigned (each compiles to a single hardware mulu.w instead of a software
 * __mulsi3 call), then reapplies the sign once at the end. Rounding is
 * round-half-away-from-zero, which differs from the original signed
 * round-half-toward-+inf by at most 1/65536 on negative results -- below the
 * precision the renderer and movement code care about.
 */
static __inline fixed atari_fixed_mul16(fixed a, fixed b)
{
	int neg = 0;
	unsigned int ua, ub;
	unsigned short al, ah, bl, bh;
	unsigned int lo, mid, hi, res;

	if (a < 0) { ua = (unsigned int)(-a); neg = 1; }  else ua = (unsigned int)a;
	if (b < 0) { ub = (unsigned int)(-b); neg ^= 1; } else ub = (unsigned int)b;

	al = (unsigned short)ua;
	ah = (unsigned short)(ua >> 16);
	bl = (unsigned short)ub;
	bh = (unsigned short)(ub >> 16);

	lo  = (unsigned int)al * (unsigned int)bl + 0x8000u;
	mid = (unsigned int)al * (unsigned int)bh + (unsigned int)ah * (unsigned int)bl;
	hi  = (unsigned int)ah * (unsigned int)bh;

	res = (hi << 16) + mid + (lo >> 16);

	return neg ? -(fixed)res : (fixed)res;
}

/*
 * FixedMul stays an out-of-line function (its body is just the inlined
 * atari_fixed_mul16 above, so it gets the hardware-mulu.w win without the
 * nested __mulsi3 calls). It is intentionally NOT declared inline here: this
 * toolchain's GNU89 __inline semantics don't emit an out-of-line copy for the
 * calls it declines to inline, which breaks linking, and m_fixed.h declares
 * FixedMul as a real function symbol.
 */
fixed FixedMul(fixed a, fixed b);
fixed FixedDiv2(fixed a, fixed b);
#elif defined(C_FIXED_MATH)
fixed FixedMul(fixed a, fixed b);
fixed FixedDiv2(fixed a, fixed b);
#else
fixed (*FixedMul) (fixed a, fixed b);
fixed (*FixedDiv2) (fixed a, fixed b);
fixed FixedMul040(fixed eins,fixed zwei);
fixed FixedDiv040(fixed eins,fixed zwei);
fixed FixedMul060(fixed eins,fixed zwei);
fixed FixedDiv060(fixed eins,fixed zwei);
#endif

fixed FixedScale(fixed orig, fixed factor, fixed divisor);
fixed FixedMulShift(fixed a, fixed b, fixed shift);
#ifdef __WATCOMC__
#pragma aux FixedMul =  \
        "imul ebx",                     \
        "add  eax, 8000h"        \
        "adc  edx,0h"            \
        "shrd eax,edx,16"       \
        parm    [eax] [ebx] \
        value   [eax]           \
        modify exact [eax edx]

#pragma aux FixedMulShift =  \
        "imul ebx",                     \
        "shrd eax,edx,cl"       \
        parm    [eax] [ebx] [ecx]\
        value   [eax]           \
        modify exact [eax edx]

#pragma aux FixedDiv2 = \
        "cdq",                          \
        "shld edx,eax,16",      \
        "sal eax,16",           \
        "idiv ebx"                      \
        parm    [eax] [ebx] \
        value   [eax]           \
        modify exact [eax edx]
#pragma aux FixedScale = \
        "imul ebx",                     \
        "idiv ecx"                      \
        parm    [eax] [ebx] [ecx]\
        value   [eax]           \
        modify exact [eax edx]
#endif

#endif
