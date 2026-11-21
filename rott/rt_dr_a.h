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
#ifndef _rt_dr_a_public
#define _rt_dr_a_public

//***************************************************************************
//
//    RT_DR_A.ASM - Low level draw stuff, DrawPost
//
//***************************************************************************

void SetMode240(void);
void RefreshClear(void);
void DrawPost (int height, char * column, char * buf);
void  DrawHeightPost (int height, byte * src, byte * buf); // IN rt_dr_a.asm
void R_DrawWallColumn (byte * buf);
#if defined(ATARI_NATIVE)
/* The native build's wall loop: R_DrawWallColumn only notes its columns
 * between these, and End draws them all (rt_scale.c). */
void ATARI_WallColumnsBegin (void);
void ATARI_WallColumnsEnd (void);
void ATARI_WallColumnsFlush (void);

/* A wall column noted for atari_col_walls (atari_col.S reads it: keep the
 * layout). shade NULL: the texture is shaded beforehand. */
typedef struct
{
	byte *dest;
	const byte *src;
	const byte *shade;
	long frac;
	long step;
	short count;
	short twice; /* 1: each pixel written twice, side by side (shade NULL) */
} atari_wallcol_t;

#define ATARI_WALLCOLS 660 /* two a column of the widest view, and spare */
extern atari_wallcol_t atari_wallcols[ATARI_WALLCOLS];
extern atari_wallcol_t *atari_wallcol_next;
#endif
void  DrawMenuPost (int height, byte * src, byte * buf); // IN rt_dr_a.asm
void  DrawMapPost (int height, byte * src, byte * buf); // IN rt_dr_a.asm

#if defined(__WATCOMC__)
#pragma aux DrawPost parm [ECX] [ESI] [EDI] modify exact [eax ebx ecx edx esi edi]
#pragma aux DrawHeightPost parm [ECX] [ESI] [EDI] modify exact [eax ebx ecx edx edi]
#pragma aux R_DrawWallColumn parm [EDI] modify exact [eax ebx ecx edx esi edi]
#pragma aux DrawMenuPost parm [ECX] [ESI] [EDI] modify exact [eax ebx ecx edx edi]
#pragma aux DrawMapPost parm [ECX] [ESI] [EDI] modify exact [eax ebx ecx edx edi]
#endif

#endif
