/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * File: r_text.c
 * Description: What the ST used to draw over its own 3D view, now drawn
 *              into the MD's: the message lines (rt_msg.c's Atari
 *              DrawMessages with rt_str.c's VW_DrawPropString), the damage
 *              border (rt_vid.c SetBorderColor) and a pause banner.
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
*/

#include <string.h>

#include "r_local.h"

char r_messages[R_MAX_MSG_LINES][R_MAX_MSG_CHARS];
int r_nummessages;
int r_bordercolor;
int r_paused;

/* VW_DrawPropString (non-DOS), clipped to the view. Returns the width. */
static int DrawPropString(int px, int py, const char *string, bool draw) {
  const font_t *font = (const font_t *)R_Lump(fontlump);
  int ch;
  int x0 = px;

  if (!font) return 0;
  const int ht = font->height;
  while ((ch = (unsigned char)*string++) != 0) {
    if (ch < 32) continue;
    ch -= 31;
    int width = (unsigned char)font->width[ch];
    const byte *source = ((const byte *)font) + font->charofs[ch];
    while (width--) {
      if (draw && px >= 0 && px < viewwidth) {
        byte *dest = r_screen + py * R_PITCH + px;
        for (int y = 0; y < ht; y++) {
          const byte pix = source[y];
          if (pix && (py + y) >= 0 && (py + y) < viewheight) *dest = pix;
          dest += R_PITCH;
        }
      }
      source += ht;
      px++;
    }
  }
  return px - x0;
}

void DrawMessages(void) {
  for (int i = 0; i < r_nummessages; i++) {
    DrawPropString(1, 2 + (i * 9), r_messages[i], true);
  }
}

void DrawPause(void) {
  static const char text[] = "PAUSED";
  const int w = DrawPropString(0, 0, text, false);
  DrawPropString((viewwidth - w) >> 1, (viewheight >> 1) - 4, text, true);
}

/* SetBorderColor: a 5-pixel frame just inside the view. */
void DrawBorder(void) {
  const byte color = (byte)r_bordercolor;
  int y;

  if (!r_bordercolor || viewwidth < 10 || viewheight < 10) return;
  for (y = 0; y < 5; y++) {
    memset(r_screen + y * R_PITCH, color, (size_t)viewwidth);
    memset(r_screen + (viewheight - 1 - y) * R_PITCH, color, (size_t)viewwidth);
  }
  for (y = 5; y < viewheight - 5; y++) {
    memset(r_screen + y * R_PITCH, color, 5);
    memset(r_screen + y * R_PITCH + viewwidth - 5, color, 5);
  }
}
