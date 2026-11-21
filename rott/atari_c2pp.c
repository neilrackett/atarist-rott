/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * atari_c2pp.c - STDL's conversion of rows in pixel pairs into eight planes
 * (its SDL2 add-on's c2ppair.c, the same assembler from c2p_asm.h), built
 * into the game for ViewZoom in 256 colours (atari_eight.c), whose zoomed
 * rows are always in pairs: 56 instructions a group of 16 against 74.
 * STDL - Atari ST DirectMedia Layer, Copyright (C) 2026 Neil Rackett,
 * LGPL-2.1-or-later: https://github.com/neilrackett/atarist-stdl
 */
#include "atari_eight.h"

#if ATARI_EIGHT && defined(__m68k__)
#include "../lib/stdl/addons/sdl2/src/c2p.h"
#include "../lib/stdl/addons/sdl2/src/c2p_asm.h"

/* atari_c2pp1 (a line a row) and atari_c2pp2 (two, a TT's) */
__asm__(C2P_PPAIR("atari_c2pp", ""));
#endif
