/**
 * File: commemul.h
 * Author: Diego Parrilla Santamaría
 * Date: March 2026
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: ROM3 communication emulator backed by a DMA ring buffer.
 */

#ifndef COMMEMUL_H
#define COMMEMUL_H

#include <inttypes.h>
#include <stdbool.h>

#include "hardware/irq.h"
#include "pico/stdlib.h"

typedef void (*CommEmulSampleCallback)(uint16_t sample);

// Returns 0 on success, < 0 on failure (PIO program load failed). The
// PIO state-machine and DMA-channel claims call the SDK's "panic on
// exhaustion" variants, so those paths abort the whole boot rather
// than returning here.
int commemul_init(void);
void __not_in_flash_func(commemul_poll)(CommEmulSampleCallback callback);

/* MD/DOOM: look at the ring WITHOUT consuming it. Advances `*cursor` (an
 * index the caller owns, start it at 0) to the DMA's current write index
 * and returns true if any sample written since the last call had
 * `window_hi` as its high byte. Lets an interrupt handler watch for one
 * kind of cart-bus read (the m68k's end-of-blit ack) independently of
 * the main loop's commemul_poll drain. */
bool __not_in_flash_func(commemul_scan)(uint32_t *cursor, uint16_t window_hi);

/* MD/ROTT: run `handler` (in RAM, on the calling core) after every ROM3
 * read. The handler must call commemul_irq_ack() before draining the ring
 * with commemul_poll(), so a sample that lands during the drain raises the
 * interrupt again rather than waiting for the next one. */
void commemul_set_irq_handler(irq_handler_t handler);
void __not_in_flash_func(commemul_irq_ack)(void);

#endif  // COMMEMUL_H
