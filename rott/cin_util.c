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
#include "cin_glob.h"
#include "modexlib.h"
#include "rt_util.h"
#include <string.h>

#ifdef DOS
#include <conio.h>
#endif

//MED
#include "memcheck.h"

static byte cinematicpal[768];
static boolean cinematicpal_valid = false;

/*
==============
=
= CinematicGetPalette
=
= Return an 8 bit / color palette
=
==============
*/

void CinematicGetPalette (byte *pal)
{
#ifdef DOS
	int	i;

   outp (PEL_READ_ADR,0);
	for (i=0 ; i<768 ; i++)
      pal[i] = ((inp (PEL_DATA))<<2);
#else
   if (cinematicpal_valid)
      memcpy(pal, cinematicpal, 768);
   else if (origpal != NULL)
      memcpy(pal, origpal, 768);
   else
      memset(pal, 0, 768);
#endif
}

/*
==============
=
= CinematicSetPalette
=
= Sets an 8 bit / color palette
=
==============
*/

void CinematicSetPalette (byte *pal)
{
   static byte lastpal[768];
   static boolean has_last = false;

   if (has_last && memcmp(lastpal, pal, 768) == 0)
      return;

   memcpy(lastpal, pal, 768);
   has_last = true;
   memcpy(cinematicpal, pal, 768);
   cinematicpal_valid = true;
#ifdef DOS
	int	i;

   outp (PEL_WRITE_ADR,0);
	for (i=0 ; i<768 ; i++)
      outp (PEL_DATA, pal[i]>>2);
#else
   SetPalette((char *)pal);
#endif
}
