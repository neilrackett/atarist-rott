/**
 * File: romemul.c
 * Author: Diego Parrilla Santamaría
 * Date: July 2023-February 2025, February 2026
 * Copyright: 2023-2026 - GOODDATA LABS SL
 * Description: C file that contains the main function of the ROM emulator.
 */

#include "romemul.h"

static int readAddrRomDmaChannel = -1;
static int lookupDataRomDmaChannel = -1;

// Default PIO to use
static PIO defaultPio = pio0;

static int initRomEmulator(PIO pio) {
  // Configure DMAs
  // Claim the first available DMA channel for read_addr_rom_dma_channel
  readAddrRomDmaChannel = dma_claim_unused_channel(true);
  DPRINTF("DMA channel for read_addr_rom_dma_channel: %d\n",
          readAddrRomDmaChannel);
  if (readAddrRomDmaChannel == -1) {
    // Handle the error, perhaps by halting the program or logging an error
    // message
    DPRINTF("Failed to claim a DMA channel for read_addr_rom_dma_channel.\n");
    dma_channel_unclaim(readAddrRomDmaChannel);
    return -1;
  }

  // Claim another available DMA channel for lookup_data_rom_dma_channel
  lookupDataRomDmaChannel = dma_claim_unused_channel(true);
  DPRINTF("DMA channel for lookup_data_rom_dma_channel: %d\n",
          lookupDataRomDmaChannel);
  if (lookupDataRomDmaChannel == -1) {
    // Handle the error
    DPRINTF("Failed to claim a DMA channel for lookup_data_rom_dma_channel.\n");
    // Optionally release the previously claimed channel if you want to clean up
    dma_channel_unclaim(lookupDataRomDmaChannel);
    return -1;
  }

  // Now, read_addr_rom_dma_channel and lookup_data_rom_dma_channel hold the
  // channel numbers for your tasks, and you can use them throughout your code.

  // Configure the read PIO state machine
  // Add the assembled program to the PIO into the memory where there are enough
  // space
  uint offsetReadROM = pio_add_program(pio, &romemul_read_program);

  // Claim a free state machine from the PIO read program
  uint smReadROM = pio_claim_unused_sm(pio, true);

  // Start the state machine, executing the PIO read program
  romemul_read_program_init(pio, smReadROM, offsetReadROM, READ_ADDR_GPIO_BASE,
                            READ_ADDR_PIN_COUNT, READ_SIGNAL_GPIO_BASE,
                            SAMPLE_DIV_FREQ);

  // Need to clear _input shift counter_, as well as FIFO, because there may be
  // partial ISR contents left over from a previous run. sm_restart does this.
  pio_sm_clear_fifos(pio, smReadROM);
  pio_sm_restart(pio, smReadROM);
  pio_sm_set_enabled(pio, smReadROM, true);

  // DMA configuration
  // Lookup data DMA: the address of the data to read from the ROM is injected
  // from the chained previous DMA channel (read_addr_rom_dma_channel) into the
  // read address trigger register. Then push the 16 bit result of the lookup
  // into the FIFO
  dma_channel_config cdmaLookup =
      dma_channel_get_default_config(lookupDataRomDmaChannel);
  channel_config_set_transfer_data_size(&cdmaLookup, DMA_SIZE_16);
  channel_config_set_read_increment(&cdmaLookup, false);
  channel_config_set_write_increment(&cdmaLookup, false);
  channel_config_set_dreq(&cdmaLookup, pio_get_dreq(pio, smReadROM, true));
  channel_config_set_chain_to(&cdmaLookup, readAddrRomDmaChannel);
  dma_channel_configure(lookupDataRomDmaChannel, &cdmaLookup,
                        &pio->txf[smReadROM], NULL, 1, false);

  // Read address DMA: the address to read from the ROM is obtained from the
  // FIFO and injected into the read address trigger register of the lookup data
  // DMA channel chained.
  dma_channel_config cdma =
      dma_channel_get_default_config(readAddrRomDmaChannel);
  channel_config_set_transfer_data_size(&cdma, DMA_SIZE_32);
  channel_config_set_read_increment(&cdma, false);
  channel_config_set_write_increment(&cdma, false);
  channel_config_set_dreq(&cdma, pio_get_dreq(pio, smReadROM, false));
  dma_channel_configure(readAddrRomDmaChannel, &cdma,
                        &dma_hw->ch[lookupDataRomDmaChannel].al3_read_addr_trig,
                        &pio->rxf[smReadROM], 1, true);

  DPRINTF("ROM emulator initialized.\n");
  return smReadROM;
}

int init_romemul(bool copyFlashToRAM) {
  // Grant high bus priority to the DMA, so it can shove the processors out
  // of the way. This should only be needed if you are pushing things up to
  // >16bits/clk here, i.e. if you need to saturate the bus completely.

#if defined(PRIORITY_DMA) && (PRIORITY_DMA == 1)
  bus_ctrl_hw->priority =
      BUSCTRL_BUS_PRIORITY_DMA_W_BITS |
      BUSCTRL_BUS_PRIORITY_DMA_R_BITS;  // DMA priority over CPU
#else
  bus_ctrl_hw->priority =
      BUSCTRL_BUS_PRIORITY_PROC0_BITS |
      BUSCTRL_BUS_PRIORITY_PROC1_BITS;  // CPU priority over DMA
#endif

  // Copy the content of the FLASH to RAM before initializing the emulator code
  // If not initialized, assume somebody else will copy "something" to RAM
  // eventually...
  if (copyFlashToRAM) {
    const uint16_t *srcAddr =
        (const uint16_t *)(XIP_BASE + FLASH_ROM_LOAD_OFFSET);
    COPY_FIRMWARE_TO_RAM(srcAddr, ROM_SIZE_WORDS * ROM_BANKS);
  }

  int smReadROM = initRomEmulator(defaultPio);
  if (smReadROM < 0) {
    DPRINTF("Error initializing ROM emulator. Error code: %d\n", smReadROM);
    return -1;
  }

  // Push to the FIFO the most significant word of the addresses used to read
  // from the emulated ROM. The PIO now consumes the 16 address lines directly,
  // so we only need to shift the RP2040 base address by 16 bits. With the
  // 64KB single-bank layout, __rom_in_ram_start__ is expected to live at
  // 0x20030000 as defined in memmap_rp.ld.

  pio_sm_put_blocking(
      defaultPio, smReadROM,
      ((unsigned long int)&__rom_in_ram_start__ >> ROMEMUL_BUS_BITS));

  // Setting the signals after configuring the PIO makes the ROM emulator to not
  // put inconsistent data in the address or data bus at any time, avoiding
  // glitches.

  // Configure the output pins for the READ and WRITE signals.
  pio_gpio_init(defaultPio, READ_SIGNAL_GPIO_BASE);
  gpio_set_dir(READ_SIGNAL_GPIO_BASE, GPIO_OUT);
  gpio_set_pulls(READ_SIGNAL_GPIO_BASE, true, false);  // Pull up (true, false)
  gpio_put(READ_SIGNAL_GPIO_BASE, 1);

  pio_gpio_init(defaultPio, WRITE_SIGNAL_GPIO_BASE);
  gpio_set_dir(WRITE_SIGNAL_GPIO_BASE, GPIO_OUT);
  gpio_set_pulls(WRITE_SIGNAL_GPIO_BASE, true, false);  // Pull up (true, false)
  gpio_put(WRITE_SIGNAL_GPIO_BASE, 1);

  // Configure the input pins for ROM4
  pio_gpio_init(defaultPio, ROM4_GPIO);
  gpio_set_dir(ROM4_GPIO, GPIO_IN);
  gpio_set_pulls(ROM4_GPIO, true, false);  // Pull up (true, false)
  gpio_pull_up(ROM4_GPIO);

  // Configure the output pins for the output data bus
  for (int i = 0; i < WRITE_DATA_PIN_COUNT; i++) {
    pio_gpio_init(defaultPio, WRITE_DATA_GPIO_BASE + i);
    gpio_set_dir(WRITE_DATA_GPIO_BASE + i, GPIO_IN);
    gpio_set_pulls(WRITE_DATA_GPIO_BASE + i, false,
                   true);  // Pull down (false, true)
    gpio_put(WRITE_DATA_GPIO_BASE + i, 0);
  }

  return smReadROM;
}
