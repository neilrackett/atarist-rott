/**
 * File: aconfig.h
 * Author: Diego Parrilla Santamaría
 * Date: February 2025, February 2026
 * Copyright: 2025-2026 - GOODDATA LABS SL
 * Description: Header file for the app configuration manager
 */

#ifndef ACONFIG_H
#define ACONFIG_H

#include "constants.h"
#include "debug.h"
#include "settings.h"

#ifdef __cplusplus
extern "C" {
#endif


#define ACONFIG_PARAM_FOLDER "FOLDER"
#define ACONFIG_PARAM_MODE "MODE"

#define ACONFIG_SUCCESS 0
#define ACONFIG_INIT_ERROR -1
#define ACONFIG_MISMATCHED_APP -2
#define ACONFIG_APPKEYLOOKUP_ERROR -3

#define LEFT_SHIFT_EIGHT_BITS 8

// Each lookup table entry is 38 bytes:
//   - 36 bytes for the UUID
//   - 2 bytes for the sector (page number)
#define ACONFIG_LOOKUP_ENTRY_SIZE 38

enum {
  ACONFIG_BUFFER_SIZE = 4096,
  ACONFIG_MAGIC_NUMBER = 0x1234,
  ACONFIG_VERSION_NUMBER = 0x0001
};

enum {
  UUID_SIZE = 36,
  UUID_POS_HYPHEN1 = 8,
  UUID_POS_HYPHEN2 = 13,
  UUID_POS_HYPHEN3 = 18,
  UUID_POS_HYPHEN4 = 23,
  UUID_POS_VERSION = 14,
  UUID_POS_VARIANT = 19
};

/**
 * @brief Initializes the application configuration settings.
 *
 * This function initializes the application configuration settings using the
 * provided default entries. If the settings are not initialized, it initializes
 * them with the default values. Searchs the flash address of the configuration
 * using the current_app_id as key in the config app lookup table
 *
 * @param current_app_id The UUID4 of the current application. Not NULL.
 * @return int Returns ACONFIG_SUCCESS on success, ACONFIG_INIT_ERROR if there
 * is an error initializing the settings, or ACONFIG_APPKEYLOOKUP_ERROR if the
 * current app ID is not found in the lookup table.
 */
int aconfig_init(const char *currentAppId);

/**
 * @brief Returns a pointer to the global settings context of the application.
 *
 * This function allows other parts of the application to retrieve a pointer
 * to the global settings context at any time.
 *
 * @return SettingsContext* Pointer to the global settings context of the
 * application
 */
SettingsContext *aconfig_getContext(void);

#ifdef __cplusplus
}
#endif

#endif  // ACONFIG_H
