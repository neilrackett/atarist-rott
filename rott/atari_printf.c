/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * atari_printf.c - printf for TOS's console: see atari_printf.h.
 */
#include <mint/osbind.h>
#include <stdarg.h>
#include <stdio.h>

#undef printf

/* Each \n not already after a \r goes out as \r\n, through GEMDOS's
 * standard output as libcmini's does (so redirection still works). */
int atari_printf(const char *fmt, ...) {
  static char buf[1024];
  static char last; /* the last character out, across calls */
  char out[128];
  va_list ap;
  int n, i, o = 0;

  va_start(ap, fmt);
  n = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (n < 0) return n;
  fflush(stdout); /* anything libcmini still holds goes first */
  for (i = 0; buf[i]; i++) {
    if (buf[i] == '\n' && last != '\r') out[o++] = '\r';
    out[o++] = last = buf[i];
    if (o >= (int)sizeof(out) - 1) {
      Fwrite(1, o, out);
      o = 0;
    }
  }
  if (o) Fwrite(1, o, out);
  return n;
}
