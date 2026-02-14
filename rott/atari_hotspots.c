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

#include "atari_hotspots.h"

#if defined(__MINT__)
#ifndef ATARI_USE_ASM_HOTSPOTS
#define ATARI_USE_ASM_HOTSPOTS 0
#endif

int ATARI_HotspotDrawWallPost(wallcast_t *post, byte *buf)
{
   (void)post;
   (void)buf;

#if ATARI_USE_ASM_HOTSPOTS
   /*
    * ASM hotspots are opt-in. Keep returning 0 until a tuned
    * implementation is dropped in, so C path remains authoritative.
    */
#endif
   return 0;
}
#endif
