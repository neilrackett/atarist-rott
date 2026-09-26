/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * File: mdtest.h
 * Description: Helpers shared by the MD/ROTT ST test apps: status reads,
 *              the 200 Hz clock, and output that goes to the screen and
 *              to a log file next to the program (so a run on real
 *              hardware can be sent back as text). Call everything from
 *              supervisor mode.
 */

#ifndef MDTEST_H
#define MDTEST_H

#include <mint/osbind.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "sidecart_md.h"

#define HZ200 (*(volatile unsigned long *)0x4BAL)

static char s_log[4096];
static int s_log_len;
static int s_fail;

static void out(const char *fmt, ...) {
  char line[256];
  va_list ap;
  int n;
  va_start(ap, fmt);
  n = vsnprintf(line, sizeof(line), fmt, ap);
  va_end(ap);
  if (n < 0) return;
  if (n >= (int)sizeof(line)) n = sizeof(line) - 1;
  (void)Cconws(line);
  if (s_log_len + n < (int)sizeof(s_log)) {
    memcpy(s_log + s_log_len, line, n);
    s_log_len += n;
  }
}

static void log_save(const char *name) {
  long fh = Fcreate(name, 0);
  if (fh < 0) return;
  Fwrite((short)fh, s_log_len, s_log);
  Fclose((short)fh);
}

static void check(int ok, const char *what) {
  out("  %-34s %s\r\n", what, ok ? "ok" : "FAIL");
  if (!ok) s_fail = 1;
}

static unsigned short status(int word) {
  unsigned short v;
  sidecart_md_bus_begin();
  v = MD_STATUS[word];
  sidecart_md_bus_end();
  return v;
}

static void wait_ticks(unsigned long ticks) {
  const unsigned long t0 = HZ200;
  while (HZ200 - t0 < ticks) {
  }
}

/* The MD takes commands in an interrupt and works through them in its
 * main loop; give it time to catch up (CMDS still for 50 ms, at most 1 s). */
static void settle(void) {
  const unsigned long t0 = HZ200;
  unsigned short last = status(MD_ST_CMDS);
  for (;;) {
    unsigned short now;
    wait_ticks(10);
    now = status(MD_ST_CMDS);
    if (now == last || HZ200 - t0 > 200) break;
    last = now;
  }
}

/* The first command gets the long timeout: the firmware may be busy. */
static int first_contact(void) {
  int rc;
  md_command_timeout = MD_TIMEOUT_DETECT;
  rc = sidecart_md_command(MD_CMD_IDLE, 0, 0);
  md_command_timeout = MD_TIMEOUT_NORMAL;
  return rc == 0;
}

#endif /* MDTEST_H */
