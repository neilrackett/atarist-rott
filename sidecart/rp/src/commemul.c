/**
 * File: commemul.c
 * Author: Diego Parrilla Santamaría
 * Date: March 2026
 * Copyright: 2026 - GOODDATA LABS SL
 * Description: ROM3 communication emulator backed by a DMA ring buffer.
 */

#include "commemul.h"

#include "commemul.pio.h"
#include "constants.h"
#include "debug.h"
#include "hardware/dma.h"
#include "hardware/pio.h"

/* MD/ROTT: ROM3 carries the whole ST -> MD command stream (up to ~2 KB
 * per command), decoded from an interrupt (commemul_set_irq_handler), so
 * the ring only has to bridge interrupt latency. 4 KB holds a whole
 * command, in case one arrives while a level pack is being programmed
 * with interrupts off (the ST should be polling the status then). */
#define COMM_RING_BITS 12u
#define COMM_RING_SIZE_BYTES (1ul << COMM_RING_BITS)
#define COMM_RING_WORDS (COMM_RING_SIZE_BYTES / sizeof(uint16_t))
#define COMM_RING_MASK (COMM_RING_WORDS - 1u)
#define COMM_DMA_TRANSFER_COUNT (0xFFFFFFFFu)

/* The DMA ring wrap needs natural alignment. */
static uint16_t commRing[COMM_RING_WORDS]
    __attribute__((aligned(COMM_RING_SIZE_BYTES)));
static uint32_t commReadIdx = 0;
static int commDmaChannel = -1;
static int commSm = -1;
static bool commInitialized = false;
static PIO commPio = pio0;

int commemul_init(void) {
  if (commInitialized) {
    return 0;
  }

  for (int i = 0; i < READ_ADDR_PIN_COUNT; i++) {
    pio_gpio_init(commPio, READ_ADDR_GPIO_BASE + i);
  }

  pio_gpio_init(commPio, READ_SIGNAL_GPIO_BASE);
  pio_gpio_init(commPio, WRITE_SIGNAL_GPIO_BASE);
  pio_gpio_init(commPio, ROM3_GPIO);

  gpio_set_dir(ROM3_GPIO, GPIO_IN);
  gpio_set_pulls(ROM3_GPIO, true, false);
  gpio_pull_up(ROM3_GPIO);

  int offset = pio_add_program(commPio, &commemul_read_program);
  if (offset < 0) {
    DPRINTF("commemul_init: pio_add_program failed (%d)\n", offset);
    return -1;
  }
  commSm = pio_claim_unused_sm(commPio, true);
  commemul_read_program_init(commPio, commSm, (uint)offset,
                             READ_ADDR_GPIO_BASE, READ_ADDR_PIN_COUNT,
                             READ_SIGNAL_GPIO_BASE, SAMPLE_DIV_FREQ);

  pio_sm_set_enabled(commPio, commSm, false);
  pio_sm_clear_fifos(commPio, commSm);
  pio_sm_restart(commPio, commSm);

  commDmaChannel = dma_claim_unused_channel(true);
  dma_channel_config c = dma_channel_get_default_config(commDmaChannel);
  channel_config_set_read_increment(&c, false);
  channel_config_set_write_increment(&c, true);
  channel_config_set_transfer_data_size(&c, DMA_SIZE_16);
  channel_config_set_ring(&c, true, COMM_RING_BITS);
  channel_config_set_dreq(&c, pio_get_dreq(commPio, commSm, false));

  dma_channel_configure(
      commDmaChannel, &c, commRing,
      (uint8_t *)(&commPio->rxf[commSm]) + 2,  // upper halfword of FIFO word
      COMM_DMA_TRANSFER_COUNT, true);

  pio_sm_set_enabled(commPio, commSm, true);
  commReadIdx = 0;
  commInitialized = true;

  DPRINTF("ROM3 comm ring initialized on pio0/sm%d dma%d (%u words / %u "
          "bytes)\n",
          commSm, commDmaChannel, (unsigned int)COMM_RING_WORDS,
          (unsigned int)COMM_RING_SIZE_BYTES);
  return 0;
}

bool __not_in_flash_func(commemul_scan)(uint32_t *cursor, uint16_t window_hi) {
  if (!commInitialized) {
    return false;
  }
  uint32_t transfersWritten =
      COMM_DMA_TRANSFER_COUNT - dma_hw->ch[commDmaChannel].transfer_count;
  uint32_t writeIdx = transfersWritten & COMM_RING_MASK;
  uint32_t idx = *cursor & COMM_RING_MASK;
  bool seen = false;
  while (idx != writeIdx) {
    if ((commRing[idx] & 0xFF00u) == window_hi) {
      seen = true;
    }
    idx = (idx + 1u) & COMM_RING_MASK;
  }
  *cursor = writeIdx;
  return seen;
}

void __not_in_flash_func(commemul_poll)(CommEmulSampleCallback callback) {
  if ((!commInitialized) || (callback == NULL)) {
    return;
  }

  uint32_t transfersWritten =
      COMM_DMA_TRANSFER_COUNT - dma_hw->ch[commDmaChannel].transfer_count;
  uint32_t writeIdx = transfersWritten & COMM_RING_MASK;

  while (commReadIdx != writeIdx) {
    callback(commRing[commReadIdx]);
    commReadIdx = (commReadIdx + 1u) & COMM_RING_MASK;
  }
}

void commemul_set_irq_handler(irq_handler_t handler) {
  /* commemul.pio raises PIO IRQ flag 0 at the end of every ROM3 cycle. */
  pio_interrupt_clear(commPio, 0);
  pio_set_irq0_source_enabled(commPio, pis_interrupt0, true);
  irq_set_exclusive_handler(PIO0_IRQ_0, handler);
  irq_set_enabled(PIO0_IRQ_0, true);
}

void __not_in_flash_func(commemul_irq_ack)(void) {
  pio_interrupt_clear(commPio, 0);
}
