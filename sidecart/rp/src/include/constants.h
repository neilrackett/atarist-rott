/**
 * File: constants.h
 * Author: Diego Parrilla Santamaría
 * Date: November 2024, February 2026
 * Copyright: 2025-2026 - GOODDATA LABS SL
 * Description: Constants used in the placeholder file
 */

#ifndef CONSTANTS_H
#define CONSTANTS_H

#include <stdint.h>

#include "hardware/vreg.h"

// Common macros
#define HEX_BASE 16
#define DEC_BASE 10

// Time macros
#define SEC_TO_MS 1000

// SELECT signal
#define SELECT_GPIO 5  // GPIO signal for SELECT

// GPIO constants for the read address from the bus
#define READ_ADDR_GPIO_BASE 6     // Start of the GPIOs for the address
#define READ_ADDR_PIN_COUNT 16    // Number of GPIOs for the address
#define READ_SIGNAL_GPIO_BASE 27  // GPIO signal for READ
#define READ_SIGNAL_PIN_COUNT 1   // Number of GPIOs for the READ signal

// Write data to the bus
#define WRITE_DATA_GPIO_BASE \
  (READ_ADDR_GPIO_BASE)  // Start of the GPIOs for the data to write
#define WRITE_DATA_PIN_COUNT \
  (READ_ADDR_PIN_COUNT)  // Number of GPIOs for the data
#define WRITE_SIGNAL_GPIO_BASE \
  (READ_SIGNAL_GPIO_BASE + 1)  // GPIO signal for WRITE
#define WRITE_SIGNAL_PIN_COUNT 1

// FLASH and RAM sections constants.
#define ROM_BANKS 1  // Number of 64KB ROM banks to emulate (ROM4 only)
#define FLASH_ROM_LOAD_OFFSET \
  0x0  // Offset start in FLASH reserved for the ROM4 image. Survives reset
       // or poweroff. Only the first 64KB are used; ROM3 is no longer a
       // data bank (it is now used as the command channel).
#define FLASH_ROM4_LOAD_OFFSET FLASH_ROM_LOAD_OFFSET  // 64KB ROM4 block
#define ROM_SIZE_BYTES 0x10000                        // 64KBytes
#define ROM_SIZE_WORDS (ROM_SIZE_BYTES / 2)           // 32KWords
#define ROM_SIZE_LONGWORDS (ROM_SIZE_BYTES / 4)       // 16KLongWords


// Frequency constants.
// 400 MHz so the Doom renderer has as much headroom as the RP2040 will
// give it (upstream rp2040-doom runs both cores at 270 MHz; here the game
// gets one core plus the c2p bottom half on the other). Requires 1.30 V
// and a /4 flash divider (see PICO_FLASH_SPI_CLKDIV in CMakeLists.txt),
// which also means XIP bandwidth is lower than upstream's; the PIO
// cart-bus programs keep their proven 225 MHz wall-clock timing via the
// scaled divider below. If a board fails to boot at 400 MHz (silicon
// lottery), step down: 360000 @ VREG_VOLTAGE_1_25, then 300000 @
// VREG_VOLTAGE_1_15, then back to 225000 @ VREG_VOLTAGE_1_10 (a /2
// flash divider is only safe at 225).
#define RP2040_CLOCK_FREQ_KHZ 400000  // Clock frequency in KHz.
// PIO clock divider: keeps the cart-bus PIO programs at their original
// 225 MHz cycle timing regardless of the sysclk overclock. Fractional
// divider jitter is <=1 sysclk (~2.5 ns) against a ~71 ns settle
// budget -- negligible.
#define SAMPLE_DIV_FREQ ((float)RP2040_CLOCK_FREQ_KHZ / 225000.0f)

// Voltage constants.
#define RP2040_VOLTAGE VREG_VOLTAGE_1_30  // Required for 400 MHz.
#define VOLTAGE_VALUES                                                 \
  (const char *[]){"NOT VALID", "NOT VALID", "NOT VALID", "NOT VALID", \
                   "NOT VALID", "NOT VALID", "0.85v",     "0.90v",     \
                   "0.95v",     "1.00v",     "1.05v",     "1.10v",     \
                   "1.15v",     "1.20v",     "1.25v",     "1.30v",     \
                   "NOT VALID", "NOT VALID", "NOT VALID", "NOT VALID", \
                   "NOT VALID"}

// This is the APP KEY that will be used to identify the current app
// It mmust be a unique UUID4 for each app, and must be the one used in the
// app.json file as descriptor of the app
#ifndef CURRENT_APP_UUID_KEY
#define CURRENT_APP_UUID_KEY "PLACEHOLDER"
#endif

// Time macros
#define GET_CURRENT_TIME() \
  (((uint64_t)timer_hw->timerawh) << 32u | timer_hw->timerawl)
#define GET_CURRENT_TIME_INTERVAL_MS(start) \
  (uint32_t)((GET_CURRENT_TIME() - start) / \
             (((uint32_t)RP2040_CLOCK_FREQ_KHZ) / 1000))

// NOLINTBEGIN(readability-identifier-naming)
extern unsigned int __flash_binary_start;
extern unsigned int _pack_flash_start;
// Size of the PACK_FLASH window, exported by memmap_rp.ld as an absolute
// symbol: MD/DOOM programs the current level's asset pack here at level
// load and the engine reads it in place over XIP. Taken from the linker
// rather than restated, so the bound the app enforces cannot drift from
// the space actually reserved. Address-of, because an absolute linker
// symbol carries its value in the address.
extern char __pack_flash_size__[];
extern unsigned int _booster_app_flash_start;
extern unsigned int _config_flash_start;
extern unsigned int _global_lookup_flash_start;
extern unsigned int _global_config_flash_start;
extern unsigned int __rom_in_ram_start__;
// NOLINTEND(readability-identifier-naming)

static inline uint32_t pack_flash_size(void) {
  return (uint32_t)(uintptr_t)__pack_flash_size__;
}

#endif  // CONSTANTS_H