/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * File: md_proto.c
 * Description: ST -> MD command transport. See md_proto.h.
 *
 * Adapted from atarist-stdoom's stdoom_protocol.c, which decoded the same
 * protocol from a DMA IRQ that fired on every cartridge read (ROM4 as well
 * as ROM3). Here ROM4 is served by md-doom's IRQ-free emulator and ROM3 is
 * captured by the commemul ring, whose PIO program raises an interrupt per
 * ROM3 read, so frame copies cost the RP nothing. stdoom's two-buffer
 * hand-off also let a second command overwrite one the main loop had not
 * taken yet; MD/ROTT's deltas must not be lost, so this is a queue.
 */

#include "md_proto.h"

#include <string.h>

#include "commemul.h"
#include "debug.h"
#include "hardware/sync.h"
#include "rott_md_protocol.h"

/* tst.b (a0,dN.w) with a0 = $FB8000: the low 16 address bits are
 * $8000 + dN, so the word sent is the sample with bit 15 flipped. */
#define MD_PROTO_ADDRESS_BIAS 0x8000u

static md_cmd_t s_queue[MD_PROTO_SLOTS];
static volatile uint32_t s_head; /* written by the interrupt */
static volatile uint32_t s_tail; /* written by the main loop */

static volatile uint32_t *s_token;
static volatile uint32_t *s_seed;
static uint32_t s_seed_state = 0x5EED1234u;

volatile uint32_t md_proto_cmds;
volatile uint32_t md_proto_chkerrs;
volatile uint32_t md_proto_drops;

/* Rolling non-zero seed (stdoom: rand() is 0 without an RTC, and a zero
 * seed breaks the ST's "seed changed" test). */
static inline uint32_t __not_in_flash_func(next_seed)(void) {
  s_seed_state = s_seed_state * 1103515245u + 12345u;
  return s_seed_state | 1u;
}

static void __not_in_flash_func(on_command)(const TransmissionProtocol *p) {
  const uint32_t head = s_head;
  if (head - s_tail >= MD_PROTO_SLOTS) {
    /* No room: no token, so the ST times out and sends it again. */
    md_proto_drops++;
    return;
  }
  md_cmd_t *c = &s_queue[head % MD_PROTO_SLOTS];
  uint16_t n = p->payload_size;
  if (n > MAX_PROTOCOL_PAYLOAD_SIZE) n = MAX_PROTOCOL_PAYLOAD_SIZE;
  c->command_id = p->command_id;
  c->payload_size = n;
  memcpy(c->payload, p->payload, n);
  __dmb();
  s_head = head + 1u;
  md_proto_cmds++;

  /* Release the ST: token back, then a fresh seed (the stub waits for
   * token == its value AND seed != its value). */
  *s_token = TPROTO_GET_RANDOM_TOKEN(p->payload);
  *s_seed = next_seed();
}

static void __not_in_flash_func(on_checksum_error)(
    const TransmissionProtocol *p) {
  (void)p;
  md_proto_chkerrs++;
}

static void __not_in_flash_func(on_sample)(uint16_t sample) {
  tprotocol_parse((uint16_t)(sample ^ MD_PROTO_ADDRESS_BIAS), on_command,
                  on_checksum_error);
}

static void __not_in_flash_func(md_proto_irq)(void) {
  commemul_irq_ack();
  commemul_poll(on_sample);
}

void md_proto_init(uintptr_t rom_base) {
  s_token = (volatile uint32_t *)(rom_base + MD_TOKEN_OFFSET);
  s_seed = (volatile uint32_t *)(rom_base + MD_SEED_OFFSET);
  s_head = 0;
  s_tail = 0;
  *s_seed = next_seed();
  commemul_set_irq_handler(md_proto_irq);
}

md_cmd_t *md_proto_peek(void) {
  if (s_tail == s_head) return NULL;
  __dmb();
  return &s_queue[s_tail % MD_PROTO_SLOTS];
}

void md_proto_pop(void) {
  __dmb();
  s_tail = s_tail + 1u;
}
