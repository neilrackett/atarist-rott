/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * File: emul.c
 * Description: MD/ROTT boot sequence. Brings up the ROM4 emulator, the ROM3
 *              command channel, Core 1 and the SD card, then hands over to
 *              md_main.c, which serves ROTT_ST.TOS's commands for ever.
 */

#include "emul.h"

#include <stdint.h>

#include "aconfig.h"
#include "commemul.h"
#include "constants.h"
#include "debug.h"
#include "ff.h"
#include "md_core1.h"
#include "md_main.h"
#include "md_proto.h"
#include "memfunc.h"
#include "pico/stdlib.h"
#include "romemul.h"
#include "sdcard.h"
#include "settings/settings.h"
#include "target_firmware.h"

void emul_start(void) {
  const char *folderName = "/rott";
  SettingsConfigEntry *folder =
      settings_find_entry(aconfig_getContext(), ACONFIG_PARAM_FOLDER);
  if (folder && folder->value[0]) folderName = folder->value;

  const uintptr_t rom_base = (uintptr_t)&__rom_in_ram_start__;

  /* Every byte the ST can see must be deterministic: clear the 64 KB, then
   * put the cartridge header back. */
  ERASE_FIRMWARE_IN_RAM();
  COPY_FIRMWARE_TO_RAM((uint16_t *)target_firmware, target_firmware_length);

  if (init_romemul(false) < 0) panic("init_romemul failed");
  if (commemul_init() < 0) panic("commemul_init failed");
  md_proto_init(rom_base);
  md_core1_init();

  static FATFS fsys;
  const bool sd_ok =
      sdcard_initFilesystem(&fsys, folderName) == SDCARD_INIT_OK;
  if (!sd_ok) DPRINTF("SD card unavailable\n");

  md_main_init(rom_base, folderName, sd_ok);
  md_main_ready();
  for (;;) {
    md_main_poll();
  }
}
