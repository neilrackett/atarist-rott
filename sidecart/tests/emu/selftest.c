/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * selftest.c - drive the MD/ROTT firmware emulator the way ROTT_ST.TOS does,
 * encoding each command word for word as sidecart_stubs.S puts it on the bus,
 * and check the answers: HELLO against the real WAD, ECHO packets (M1's
 * "no dropped bytes over 10,000 packets"), a test-pattern frame.
 *
 *   selftest <sd_root> <out_dir>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mdemu.h"
#include "rott_md_protocol.h"

static uint16_t rom4w(uint32_t off) { return mdemu_rom4_word(off); }
static uint32_t rom4l(uint32_t off) {  /* a 68000 move.l */
  return ((uint32_t)rom4w(off) << 16) | rom4w(off + 2);
}
static uint16_t st(int word) { return rom4w(MD_STATUS_OFFSET + 2 * word); }

static void bus(uint16_t w) { mdemu_rom3_read((0x8000u + w) & 0xFFFFu); }

static int wait_token(uint32_t d2) {
  for (int i = 0; i < 1000; i++) {
    if (rom4l(MD_TOKEN_OFFSET) == d2 && rom4l(MD_SEED_OFFSET) != d2) return 0;
  }
  return -1;
}

/* send_sync_command_to_sidecart with d1 = 8 */
static int send_sync(uint16_t cmd, uint32_t d3, uint32_t d4) {
  const uint32_t d2 = rom4l(MD_SEED_OFFSET);
  const uint16_t size = 8 + 4;
  const uint16_t w[] = {cmd, size, (uint16_t)d2, (uint16_t)(d2 >> 16),
                        (uint16_t)d3, (uint16_t)(d3 >> 16), (uint16_t)d4,
                        (uint16_t)(d4 >> 16)};
  uint16_t sum = 0;
  bus(0xABCD);
  for (unsigned i = 0; i < 8; i++) {
    sum += w[i];
    bus(w[i]);
  }
  bus(sum);
  return wait_token(d2);
}

/* send_sync_write_command_to_sidecart: header, then the buffer as the ST
 * holds it in memory (big-endian words). */
static int send_write(uint16_t cmd, const uint8_t *buf, unsigned bytes,
                      uint32_t d3, uint32_t d4, uint32_t d5, int corrupt) {
  const uint32_t d2 = rom4l(MD_SEED_OFFSET);
  const uint16_t size = (uint16_t)((16 + bytes + 1) & ~1u);
  const uint16_t h[] = {cmd,
                        size,
                        (uint16_t)d2,
                        (uint16_t)(d2 >> 16),
                        (uint16_t)d3,
                        (uint16_t)(d3 >> 16),
                        (uint16_t)d4,
                        (uint16_t)(d4 >> 16),
                        (uint16_t)d5,
                        (uint16_t)(d5 >> 16)};
  uint16_t sum = 0;
  bus(0xABCD);
  for (unsigned i = 0; i < 10; i++) {
    sum += h[i];
    bus(h[i]);
  }
  for (unsigned i = 0; i < bytes; i += 2) {
    uint16_t w = (uint16_t)(buf[i] << 8);
    if (i + 1 < bytes) w |= buf[i + 1];
    sum += w;
    bus(corrupt && i == 2 ? (uint16_t)(w ^ 1) : w);
  }
  bus(sum);
  return wait_token(d2);
}

static int fails;
#define CHECK(c, ...)                   \
  do {                                  \
    if (!(c)) {                         \
      printf("FAIL: " __VA_ARGS__);     \
      printf("\n");                     \
      fails++;                          \
    }                                   \
  } while (0)

int main(int argc, char **argv) {
  const char *sd = argc > 1 ? argv[1] : ".";
  const char *out = argc > 2 ? argv[2] : ".";
  uint8_t buf[MD_CMD_MAX_BYTES];

  mdemu_init(sd);
  mdemu_dump_frames(out, 1);
  CHECK((mdemu_rom4_word(MD_READY_OFFSET) & 0xFF) == MD_READY_MAGIC, "ready");
  CHECK(st(MD_ST_MAGIC) == MD_STATUS_MAGIC, "status magic");

  /* HELLO with the WAD's real header */
  {
    char path[1024];
    uint8_t hdr[12];
    snprintf(path, sizeof(path), "%s/rott/HUNTBGIN.WAD", sd);
    FILE *f = fopen(path, "rb");
    CHECK(f != NULL, "open %s", path);
    if (!f) return 1;
    fread(hdr, 1, 12, f);
    fseek(f, 0, SEEK_END);
    const uint32_t size = (uint32_t)ftell(f);
    fclose(f);
    uint16_t hello[MD_HELLO_WORDS];
    memset(hello, 0, sizeof(hello));
    hello[MD_HELLO_NUMLUMPS] = (uint16_t)(hdr[4] | hdr[5] << 8);
    md_put32(hello + MD_HELLO_WADSIZE, size);
    md_put32(hello + MD_HELLO_DIROFS,
             hdr[8] | hdr[9] << 8 | hdr[10] << 16 | (uint32_t)hdr[11] << 24);
    /* serialise as the big-endian ST would hold it */
    uint8_t *p = buf;
    for (unsigned i = 0; i < MD_HELLO_NAME; i++) {
      *p++ = (uint8_t)(hello[i] >> 8);
      *p++ = (uint8_t)hello[i];
    }
    memset(p, 0, MD_HELLO_NAME_WORDS * 2);
    strcpy((char *)p, "HUNTBGIN.WAD");
    CHECK(send_write(MD_CMD_HELLO, buf, MD_HELLO_WORDS * 2, MD_HELLO_MAGIC,
                     MD_PROTOCOL_VERSION, 0, 0) == 0,
          "HELLO token");
    char result[64];
    for (int i = 0; i < 63; i++) {
      const uint16_t w = rom4w(MD_RESULT_OFFSET + (i & ~1));
      result[i] = (char)((i & 1) ? w : w >> 8);
      if (!result[i]) break;
    }
    result[63] = 0;
    printf("HELLO: \"%s\", errors %04x\n", result, st(MD_ST_ERRORS));
    CHECK(st(MD_ST_ERRORS) == 0, "HELLO errors %04x", st(MD_ST_ERRORS));
  }

  /* ECHO: 10,000 packets of random sizes, then one corrupted. */
  {
    srand(1);
    for (int n = 0; n < 10000; n++) {
      const unsigned words = 1 + (unsigned)rand() % (MD_CMD_MAX_BYTES / 2);
      uint32_t sum = 0;
      for (unsigned i = 0; i < words; i++) {
        const uint16_t w = (uint16_t)rand();
        buf[2 * i] = (uint8_t)(w >> 8);
        buf[2 * i + 1] = (uint8_t)w;
        sum += w;
      }
      if (send_write(MD_CMD_ECHO, buf, words * 2, (uint32_t)n, sum, words, 0)) {
        CHECK(0, "ECHO %d token", n);
        break;
      }
    }
    printf("ECHO: ok %u bad %u, commands %u, checksum errors %u, drops %u\n",
           st(MD_ST_ECHO_OK), st(MD_ST_ECHO_BAD), st(MD_ST_CMDS),
           st(MD_ST_CHKERRS), st(MD_ST_DROPS));
    CHECK(st(MD_ST_ECHO_OK) == 10000 && st(MD_ST_ECHO_BAD) == 0, "ECHO counts");
    CHECK(send_write(MD_CMD_ECHO, buf, 64, 0, 0, 32, 1) != 0,
          "a corrupted command must not be acknowledged");
    CHECK(st(MD_ST_CHKERRS) == 1, "checksum error counted");
  }

  /* TEST: a pattern frame, published like a real one. */
  {
    uint16_t t[MD_TEST_WORDS] = {0, 16};
    uint8_t b[4] = {(uint8_t)(t[0] >> 8), (uint8_t)t[0], (uint8_t)(t[1] >> 8),
                    (uint8_t)t[1]};
    CHECK(send_write(MD_CMD_TEST, b, 4, 1, 1, (320u << 16) | 168u, 0) == 0,
          "TEST token");
    CHECK(st(MD_ST_READY_SEQ) == 1, "TEST published (seq %u)", st(MD_ST_READY_SEQ));
    CHECK(st(MD_ST_VIEW_W) == 320 && st(MD_ST_VIEW_H) == 168, "TEST size");
    CHECK(send_sync(MD_CMD_IDLE, 0, 0) == 0, "IDLE token");
  }

  printf(fails ? "selftest: %d failure(s)\n" : "selftest: OK\n", fails);
  return fails ? 1 : 0;
}
