/**
 * File: main.c
 * Author: Diego Parrilla Santamaría
 * Date: February 2025, February 2026
 * Copyright: 2023-2026 - GOODDATA LABS SL
 * Description: Main file for an app.
 */

#include "aconfig.h"
#include "constants.h"
#include "debug.h"
#include "emul.h"
#include "gconfig.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "pico/stdlib.h"
#include "reset.h"
#include "select.h"

// This is the main.c file for the app or microfirmware. It is the entry point
// for the application. It is the first file that is executed when the
// application is started. It is responsible for setting up the hardware and
// software environment for the application to run. It is also responsible for
// initializing the global configuration and application settings. If the
// settings are not initialized, it jumps to the booster app to initialize them.
// If the settings are initialized, it starts the application.
// A good practice is to keep the main.c file as simple as possible. It should
// not be necessary to modify this file when adding new features to the
// application. Instead, new features should be implemented in separate files
// and functions.
//
// The last line of the main function calls the emul_start function, which is
// the entry point for the application. This function is responsible for
// starting the application and running the main loop. This the function that
// should be modified when adding new features to the application.

int main() {
  // Raise the core voltage BEFORE the clock -- the chip must never run
  // the higher frequency at the lower voltage, even transiently. (The
  // template's original order only worked because it asked for the
  // power-on default 1.10 V.) Be cautious with the voltage: I don't
  // think it's possible to damage the hardware, but it's possible to
  // make it unstable.
  vreg_set_voltage(RP2040_VOLTAGE);
  sleep_ms(10);  // Let the regulator settle at the new voltage.

  // Set the clock frequency. Keep in mind that if you are managing remote
  // commands you should overclock the CPU to >=225MHz.
  set_sys_clock_khz(RP2040_CLOCK_FREQ_KHZ, true);

  // A note about outputting debug information through the UART. It's not
  // recommended to output debug information through the UART in a production
  // environment in the callback functions of the DMAs used to communicate with
  // the remote device. This is because the UART is not a real-time interface
  // and can introduce delays in the execution of the code. This can cause
  // problems when the code is time-sensitive.
#if defined(_DEBUG) && (_DEBUG != 0)
  // Initialize chosen serial port
  stdio_init_all();
  setvbuf(stdout, NULL, _IONBF,
          1);  // specify that the stream should be unbuffered

  // Only startup information to display
  // You should modify this to show the information you need
  DPRINTF("\n\nApp. %s (%s). %s mode.\n\n", RELEASE_VERSION, RELEASE_DATE,
          _DEBUG ? "DEBUG" : "RELEASE");

  // Show information about the frequency and voltage
  int currentClockFrequencyKhz = RP2040_CLOCK_FREQ_KHZ;
  const char *currentVoltage = VOLTAGE_VALUES[RP2040_VOLTAGE];
  DPRINTF("Clock frequency: %i KHz\n", currentClockFrequencyKhz);
  DPRINTF("Voltage: %s\n", currentVoltage);
  DPRINTF("FLASH SPI CLKDIV: %i\n", PICO_FLASH_SPI_CLKDIV);
  DPRINTF("PICO_FLASH_SIZE_BYTES: %i\n", PICO_FLASH_SIZE_BYTES);

  // Show information about the flash memory layout
  unsigned int flashLength = (unsigned int)&_booster_app_flash_start -
                             (unsigned int)&__flash_binary_start;
  unsigned int boosterFlashLength = (unsigned int)&_config_flash_start -
                                    (unsigned int)&_booster_app_flash_start;
  unsigned int configFlashLength = (unsigned int)&_global_lookup_flash_start -
                                   (unsigned int)&_config_flash_start;
  unsigned int globalLookupFlashLength = FLASH_SECTOR_SIZE;
  unsigned int globalConfigFlashLength = FLASH_SECTOR_SIZE;
  unsigned int romInRamLength = ROM_SIZE_BYTES * ROM_BANKS;
  unsigned int packFlashLength = pack_flash_size();

  DPRINTF("Flash start: 0x%X, length: %u bytes\n",
          (unsigned int)&__flash_binary_start, flashLength);
  DPRINTF("Pack flash start: 0x%X, length: %u bytes\n",
          (unsigned int)&_pack_flash_start, packFlashLength);
  DPRINTF("Booster Flash start: 0x%X, length: %u bytes\n",
          (unsigned int)&_booster_app_flash_start, boosterFlashLength);
  DPRINTF("Config Flash start: 0x%X, length: %u bytes\n",
          (unsigned int)&_config_flash_start, configFlashLength);
  DPRINTF("Global Lookup Flash start: 0x%X, length: %u bytes\n",
          (unsigned int)&_global_lookup_flash_start, globalLookupFlashLength);
  DPRINTF("Global Config Flash start: 0x%X, length: %u bytes\n",
          (unsigned int)&_global_config_flash_start, globalConfigFlashLength);
  DPRINTF("ROM in RAM start: 0x%X, length: %u bytes\n",
          (unsigned int)&__rom_in_ram_start__, romInRamLength);

#endif

  // Hardware escape hatch: if the cartridge SELECT button is held at
  // power-on, jump straight to the Booster app before any global/app
  // config or emulation runs. This guarantees the user can always reach
  // the configurator, even if this app's config is wrong or it misbehaves.
  select_configure();
  if (select_detectPush()) {
    DPRINTF("SELECT held at power-on. Jump to Booster application\n");
    reset_jump_to_booster();
  }

  // The same escape hatch from software: the game's "Booster" menu item
  // reboots with this request set, so the jump is made from a clean
  // machine rather than one with the cartridge emulation still running.
  if (reset_booster_requested()) {
    DPRINTF("Booster requested before this reboot. Jump to Booster\n");
    reset_jump_to_booster();
  }

  DPRINT_HEAP();  // Boot heap diagnosis -- see debug.h.

  // Load the global configuration parameters
  int err = gconfig_init(CURRENT_APP_UUID_KEY);
  // If the global settings are not intialized, jump to the booster app to
  // initialize them
  if (err < 0) {
    DPRINTF("Settings not initialized. Jump to Booster application\n");
    reset_jump_to_booster();
  }

  // If we are here, it means the app uuid key is correct. So we can read or
  // initialize the app settings
  err = aconfig_init(CURRENT_APP_UUID_KEY);
  switch (err) {
    case ACONFIG_SUCCESS:
      DPRINTF("App settings found and already initialized\n");
      break;
    case ACONFIG_APPKEYLOOKUP_ERROR:
      // The app key is not found in the lookup table. Go to booster.
      DPRINTF("App key not found in the lookup table. Go to BOOSTER.\n");
      reset_jump_to_booster();
      // We should never reach this point
      break;
    case ACONFIG_INIT_ERROR:
      // No settings found. First time the app is executed? Then initialize the
      // settings
      DPRINTF("App settings not initialized. Initialize them first\n");
      err = settings_save(aconfig_getContext(), true);
      if (err < 0) {
        // Something went wrong saving the settings. Go to booster.
        DPRINTF("Error saving settings. Go to BOOSTER.\n");
        reset_jump_to_booster();
        // We should never reach this point
      }
      // Show the settings
      settings_print(aconfig_getContext(), NULL);
      break;
  }

  // Start the application
  emul_start();
}
