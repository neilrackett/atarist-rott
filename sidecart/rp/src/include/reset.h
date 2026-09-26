/**
 * File: reset.h
 * Author: Diego Parrilla Santamaría
 * Date: December 2025, February 2026
 * Copyright: 2024-2026 - GOODDATA LABS SL
 * Description: Header file for RESET functions of the booster app
 */

#ifndef RESET_H
#define RESET_H

#include "constants.h"
#include "debug.h"
#include "gconfig.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "settings.h"

#define RESET_WATCHDOG_TIMEOUT 20  // 20 ms

/* MD/DOOM: "go to the Booster" from inside a running app. Jumping there
 * directly would hand the Booster a chip with this app's PIO, DMA and
 * second core still live -- the proven jump in main() happens before
 * any of that is set up. So reboot instead, leaving a note in a
 * watchdog scratch register (untouched by a watchdog reset, and by
 * watchdog_reboot, which only uses 4..7), and let main() make the jump
 * on the way back up from a clean machine. */
#define RESET_BOOSTER_REQUEST_MAGIC 0xB0057E12u

/* Watchdog scratch usage across a reboot. 0 is the Booster request
 * above; 1 counts consecutive reboots caused by a lost ST (emul.c); 2
 * carries "the user quit, so skip the autostart when the ST comes back"
 * from I_Quit to the next boot. */
#define RESET_SKIP_AUTOSTART_MAGIC 0x5C1D0FF0u

/**
 * @brief Reboot, and jump to the Booster app once back up.
 *
 * @note This function does not return.
 */
static inline void __attribute__((noreturn)) reset_reboot_to_booster(void) {
  watchdog_hw->scratch[0] = RESET_BOOSTER_REQUEST_MAGIC;
  save_and_disable_interrupts();
  watchdog_reboot(0, 0, RESET_WATCHDOG_TIMEOUT);
  while (1) {
    tight_loop_contents();
  }
}

/**
 * @brief Was this boot asked to go straight to the Booster?
 *
 * Consumes the request, so a later plain reset boots the app as usual.
 */
static inline bool reset_booster_requested(void) {
  if (watchdog_hw->scratch[0] != RESET_BOOSTER_REQUEST_MAGIC) {
    return false;
  }
  watchdog_hw->scratch[0] = 0;
  return true;
}

/**
 * @brief Reset the current app and jump to the Booster app in flash
 *
 *
 * Do not use to reboot the device, because it does not jump to the start of the
 * Flash
 *
 * @note This function should not return. If it does, an error message is
 * printed.
 */
static inline void reset_jump_to_booster(void) {
  // This code jumps to the Booster application at the top of the flash memory.
  // The reason to perform this jump is for performance reasons.
  // It should be placed at the beginning of main() if the SELECT signal or
  // BOOSTER app is selected. Set VTOR register, set stack pointer, and jump to
  // reset.
  __asm__ __volatile__(
      "mov r0, %[start]\n"
      "ldr r1, =%[vtable]\n"
      "str r0, [r1]\n"
      "ldmia r0, {r0, r1}\n"
      "msr msp, r0\n"
      "bx r1\n"
      :
      : [start] "r"((unsigned int)&_booster_app_flash_start + 256),
        [vtable] "X"(PPB_BASE + M0PLUS_VTOR_OFFSET)
      :);
  DPRINTF("You should never reach this point\n");
}

/**
 * @brief Reset the app and reentry in the main device app in flash
 *
 *
 * Use to reboot the device, it jumps to the start of the Flash
 *
 * @note This function should not return. If it does, an error message is
 * printed.
 */
void __attribute__((noreturn)) reset_device();

/**
 * @brief Reset the app and reentry in the main device app in flash.
 *       Erase the flash memory before rebooting
 *
 *
 * Use to reboot the device to a fabric configuration, it jumps to the start of
 * the Flash
 *
 * @note This function should not return. If it does, an error message is
 * printed.
 */
void __attribute__((noreturn)) reset_deviceAndEraseFlash();

#endif  // RESET_H
