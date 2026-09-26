/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * File: md_proto.h
 * Description: ST -> MD command transport. The ROM3 capture ring
 *              (commemul) raises an interrupt after every cart-bus read;
 *              the handler runs atarist-stdoom's tprotocol parser over the
 *              new samples, queues each complete, checksummed command and
 *              releases the ST by writing its token back straight away.
 *
 * The ST therefore never waits for the MD to finish anything: a command
 * costs exactly its upload time. Commands are consumed in order by the
 * main loop with md_proto_peek() / md_proto_pop(). When the queue is full
 * a command is dropped WITHOUT the token write, so the ST times out and
 * resends it -- every command is idempotent for that reason.
 */

#ifndef MD_PROTO_H
#define MD_PROTO_H

#include <stdbool.h>
#include <stdint.h>

#include "pico.h"
#include "tprotocol.h"

/* Queue depth. Two FRAMEs can be in flight when pipelined, plus a WORLD
 * that spilled out of one. */
#define MD_PROTO_SLOTS 4

typedef struct {
  uint16_t command_id;
  uint16_t payload_size; /* bytes, including the 4-byte token */
  /* Payload as the parser stored it: 16-bit words in RP order. Word 0..1
   * is the token, then the command's own words. */
  uint16_t payload[(MAX_PROTOCOL_PAYLOAD_SIZE) / 2] __attribute__((aligned(4)));
} md_cmd_t;

/* Counters for the status block. */
extern volatile uint32_t md_proto_cmds;
extern volatile uint32_t md_proto_chkerrs;
extern volatile uint32_t md_proto_drops;

/* Initialise the queue and the token/seed, and hook the ROM3 interrupt.
 * `rom_base` is the RP address of the ROM4 window ($FA0000). Call after
 * commemul_init(). */
void md_proto_init(uintptr_t rom_base);

/* Oldest queued command, or NULL. Stays valid until md_proto_pop(). */
md_cmd_t *md_proto_peek(void);
void md_proto_pop(void);

/* Payload helpers: the command's words after the token. */
static inline const uint16_t *md_cmd_words(const md_cmd_t *c) {
  return c->payload + 2;
}
static inline uint32_t md_cmd_word_count(const md_cmd_t *c) {
  return c->payload_size >= 4u ? (uint32_t)(c->payload_size - 4u) / 2u : 0u;
}

#endif /* MD_PROTO_H */
