#include "include/aconfig.h"

// MD/ROTT: the folder holds the WAD the level packs are built from.

static const SettingsConfigEntry defaultEntries[] = {
    {ACONFIG_PARAM_FOLDER, SETTINGS_TYPE_STRING, "/rott"},
    {ACONFIG_PARAM_MODE, SETTINGS_TYPE_INT, "255"},  // 255: Menu mode
};

// Create a global context for our settings
static SettingsContext gSettingsCtx;

// Helper function to check if a UUID_SIZE-character string is a valid UUID4.
// A valid UUID4 is in the canonical format
// "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx" where x is a hexadecimal digit and y
// is one of [8, 9, A, B] (case-insensitive).
static int isValidUuid4(const char *uuid) {
  // We assume "uuid" is exactly UUID_SIZE characters (no null terminator at
  // index UUID_SIZE) Positions of hyphens and specific checks:
  //   Hyphens at indices UUID_POS_HYPHEN1, UUID_POS_HYPHEN2, UUID_POS_HYPHEN3,
  //   UUID_POS_HYPHEN4. The character at index UUID_POS_VERSION must be '4'.
  //   The character at index UUID_POS_VARIANT must be one of '8', '9', 'A', or
  //   'B' (case-insensitive).

  // Quick initial checks for mandatory hyphens.
  if (uuid[UUID_POS_HYPHEN1] != '-' || uuid[UUID_POS_HYPHEN2] != '-' ||
      uuid[UUID_POS_HYPHEN3] != '-' || uuid[UUID_POS_HYPHEN4] != '-') {
    return 0;
  }

  // Check the version char is '4'.
  if (uuid[UUID_POS_VERSION] != '4') {
    return 0;
  }

  // Check the variant char is one of '8', '9', 'A', or 'B' (case-insensitive).
  char variant = uuid[UUID_POS_VARIANT];
  switch (variant) {
    case '8':
    case '9':
    case 'A':
    case 'B':
    case 'a':
    case 'b':
      break;
    default:
      return 0;
  }

  // Check the rest of the characters for hex digits
  // (excluding the four known hyphen positions)
  static const int hyphenPositions[4] = {UUID_POS_HYPHEN1, UUID_POS_HYPHEN2,
                                         UUID_POS_HYPHEN3, UUID_POS_HYPHEN4};
  int jdx = 0;  // index into hyphen_positions

  for (int i = 0; i < UUID_SIZE; i++) {
    if (jdx < 4 && i == hyphenPositions[jdx]) {
      // we already checked these are '-', so skip
      jdx++;
      continue;
    }
    // Check for hex digit
    if (!isxdigit((unsigned char)uuid[i])) {
      return 0;
    }
  }

  return 1;
}

int aconfig_init(const char *currentAppId) {
  DPRINTF("Finding the configuration flash address for the current app\n");

  // Calculate the lookup table boundaries
  uint8_t *lookupStart = (uint8_t *)&_global_lookup_flash_start;
  uint8_t *lookupEnd =
      (uint8_t *)&_global_config_flash_start;  // one past the last valid lookup
                                               // byte
  size_t lookupLen = (size_t)(lookupEnd - lookupStart);

  // Loop through each lookup entry
  //    ACONFIG_LOOKUP_ENTRY_SIZE bytes per entry:
  //      first UUID_SIZE bytes => UUID
  //      next 2 bytes  => sector index
  //    Stop on zero-filled UUID or invalid data or out-of-bounds.
  uint8_t *ptr = lookupStart;
  uint32_t flashAddress = 0;  // Will remain 0 if we don't find a match

  while ((size_t)(ptr - lookupStart) + ACONFIG_LOOKUP_ENTRY_SIZE <= lookupLen) {
    DPRINTF("Lookup entry at %X is %s\n", ptr, (const char *)ptr);

    // If the first byte is 0, we consider there are no more valid entries
    if (ptr[0] == 0) {
      break;
    }

    // Check if the UUID_SIZE bytes form a valid UUID4
    // If invalid, assume no further valid data
    if (!isValidUuid4((const char *)ptr)) {
      break;
    }

    // Compare with the given current_app_id
    if (memcmp(ptr, currentAppId, UUID_SIZE) == 0) {
      // We found our app ID
      // Next two bytes (little-endian) represent the sector number
      uint16_t sector =
          (uint16_t)(ptr[UUID_SIZE]) |
          ((uint16_t)(ptr[UUID_SIZE + 1]) << LEFT_SHIFT_EIGHT_BITS);

      // Convert sector number to actual flash address
      flashAddress =
          (uint32_t)&_config_flash_start + (sector * FLASH_SECTOR_SIZE);
      DPRINTF("Configuration flash address found sector:%u addr: 0x%X\n",
              sector, flashAddress);
      break;
    }

    // Move pointer to the next entry
    ptr += ACONFIG_LOOKUP_ENTRY_SIZE;
  }

  if (flashAddress == 0) {
    DPRINTF("Configuration flash address not found for the current app\n");
    return ACONFIG_APPKEYLOOKUP_ERROR;
  }

  DPRINTF("Initializing app settings\n");
  int err = settings_init(&gSettingsCtx, defaultEntries,
                          sizeof(defaultEntries) / sizeof(defaultEntries[0]),
                          flashAddress - XIP_BASE, ACONFIG_BUFFER_SIZE,
                          ACONFIG_MAGIC_NUMBER, ACONFIG_VERSION_NUMBER);

  // If the settings are not initialized, then we must initialize them with the
  // default values in the Booster application
  if (err < 0) {
    DPRINTF("Error initializing app settings.\n");
    return ACONFIG_INIT_ERROR;
  }

  DPRINTF("Settings app loaded.\n");

  settings_print(&gSettingsCtx, NULL);

  return ACONFIG_SUCCESS;
}

SettingsContext *aconfig_getContext(void) { return &gSettingsCtx; }