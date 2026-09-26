/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * File: pingtest.c
 * Description: MD/ROTT milestone M1 transport test (PINGTEST.TOS).
 *
 * Needs only the MD/ROTT firmware running on the Multi-device; no WAD.
 *   1. Detection: the ROM4 ready magic, status block, protocol version,
 *      the firmware's result text and its main-loop heartbeat.
 *   2. Sync commands: round-trip time of IDLE (ROM3 upload + token).
 *   3. ECHO: payloads of several sizes with a checksum the MD verifies,
 *      so any word lost or corrupted on the bus shows as ECHO_BAD; gives
 *      the upload rate.
 * Prints PASS or FAIL, saves the same text as PINGTEST.TXT and waits for
 * a key. Build: make -C sidecart tests. GPL-2.0-or-later like the rott/
 * client code it links.
 */

#include "mdtest.h"

#define PING_COUNT 1000
#define ECHO_SIZES 4

static unsigned short s_buf[MD_CMD_MAX_BYTES / 2];

static void test_detect(void) {
  char text[MD_RESULT_SIZE];
  unsigned short hb;
  unsigned long t0;

  out("Detection\r\n");
  check(1, "ready magic in ROM4");
  check(status(MD_ST_MAGIC) == MD_STATUS_MAGIC, "status block magic");
  out("  protocol %u (ST expects %u)\r\n", status(MD_ST_PROTO),
      (unsigned)MD_PROTOCOL_VERSION);
  check(status(MD_ST_PROTO) == MD_PROTOCOL_VERSION, "protocol version");
  sidecart_md_result(text, sizeof(text));
  out("  firmware: %s\r\n", text);
  hb = status(MD_ST_HEARTBEAT);
  t0 = HZ200;
  while (status(MD_ST_HEARTBEAT) == hb && HZ200 - t0 < 20) {
  }
  check(status(MD_ST_HEARTBEAT) != hb, "main loop heartbeat");
}

static void test_ping(void) {
  unsigned long t0, t1;
  unsigned short cmds0, fails0;
  int i, bad = 0;

  out("Sync commands (IDLE x %d)\r\n", PING_COUNT);
  if (!first_contact()) {
    check(0, "first command answered");
    return;
  }
  settle();
  cmds0 = status(MD_ST_CMDS);
  fails0 = md_command_failures;
  t0 = HZ200;
  for (i = 0; i < PING_COUNT; i++) {
    if (sidecart_md_command(MD_CMD_IDLE, (long)i, 0)) bad++;
  }
  t1 = HZ200;
  settle();
  out("  %lu us per command, %d failed after retries\r\n",
      ((t1 - t0) * 5000UL) / PING_COUNT, bad);
  check(bad == 0 && md_command_failures == fails0, "all answered");
  /* A retried command reaches the MD twice, so it may count more. */
  check((unsigned short)(status(MD_ST_CMDS) - cmds0) >= PING_COUNT,
        "MD counted them");
}

static void test_echo(void) {
  static const int sizes[ECHO_SIZES] = {16, 256, 1024, MD_CMD_MAX_BYTES};
  unsigned short ok0, bad0, chk0, drop0;
  unsigned long seed = 0x12345678UL;
  int s;

  out("ECHO (payload checked by the MD)\r\n");
  settle();
  ok0 = status(MD_ST_ECHO_OK);
  bad0 = status(MD_ST_ECHO_BAD);
  chk0 = status(MD_ST_CHKERRS);
  drop0 = status(MD_ST_DROPS);
  for (s = 0; s < ECHO_SIZES; s++) {
    const int bytes = sizes[s];
    const int words = bytes / 2;
    const int count = bytes >= 1024 ? 100 : 300;
    unsigned long total = 0;
    int i, j, bad = 0;

    for (i = 0; i < count; i++) {
      unsigned long sum = 0;
      unsigned long t0;
      for (j = 0; j < words; j++) {
        seed = seed * 1103515245UL + 12345UL;
        s_buf[j] = (unsigned short)(seed >> 16);
        sum += s_buf[j];
      }
      t0 = HZ200;
      if (sidecart_md_write(MD_CMD_ECHO, s_buf, bytes, (long)i, (long)sum,
                            (long)words)) {
        bad++;
      }
      total += HZ200 - t0;
    }
    if (!total) total = 1;
    out("  %4d bytes x %3d: %lu KB/s, %d failed\r\n", bytes, count,
        ((unsigned long)bytes * count * 200UL / 1024UL) / total, bad);
    if (bad) s_fail = 1;
  }
  settle();
  out("  MD: ok %u, bad %u, checksum errors %u, drops %u\r\n",
      (unsigned)(unsigned short)(status(MD_ST_ECHO_OK) - ok0),
      (unsigned)(unsigned short)(status(MD_ST_ECHO_BAD) - bad0),
      (unsigned)(unsigned short)(status(MD_ST_CHKERRS) - chk0),
      (unsigned)(unsigned short)(status(MD_ST_DROPS) - drop0));
  check(status(MD_ST_ECHO_BAD) == bad0, "no corrupted payloads");
  check(status(MD_ST_ECHO_OK) != ok0, "payloads verified");
}

int main(void) {
  long ssp = Super(0L);

  (void)Cconws("\033E");
  out("ROTT Accelerator PINGTEST\r\n\r\n");
  if (!sidecart_md_present()) {
    out("ROTT Accelerator not found (no ready\r\n"
        "magic in ROM4). Is it selected in\r\n"
        "Booster?\r\n");
    s_fail = 1;
  } else {
    test_detect();
    test_ping();
    test_echo();
  }
  out("\r\n%s\r\n", s_fail ? "FAIL" : "PASS");
  log_save("PINGTEST.TXT");
  Super((void *)ssp);
  (void)Cconws("Saved PINGTEST.TXT. Press a key.\r\n");
  (void)Cconin();
  return s_fail;
}
