#include "sdcard.h"

static FATFS *mountedFsPtr = NULL;
static bool sdMounted = false;

static void sdcard_warnDebugRisk(void) {
  size_t sdCount = sd_get_num();
  for (size_t i = 0; i < sdCount; i++) {
    sd_card_t *sdCard = sd_get_by_num(i);
    if ((sdCard != NULL) && !sdCard->use_card_detect) {
      DPRINTF(
          "WARNING: SD card-detect disabled on slot %u. "
          "When debugging, starting without an SD card can trigger assertions "
          "during init.\n",
          (unsigned)i);
    }
  }
}

static sdcard_status_t sdcardInit() {
  DPRINTF("Initializing SD card...\n");
  sdcard_warnDebugRisk();
  // Initialize the SD card
  bool success = sd_init_driver();
  if (!success) {
    DPRINTF("ERROR: Could not initialize SD card\r\n");
    return SDCARD_INIT_ERROR;
  }
  DPRINTF("SD card initialized.\n");

  sdcard_setSpiSpeedSettings();
  return SDCARD_INIT_OK;
}

FRESULT sdcard_mountFilesystem(FATFS *fsys, const char *drive) {
  // Mount the drive
  FRESULT fres = f_mount(fsys, drive, 1);
  if (fres != FR_OK) {
    DPRINTF("ERROR: Could not mount the filesystem. Error code: %d\n", fres);
  } else {
    DPRINTF("Filesystem mounted.\n");
  }
  return fres;
}

bool sdcard_dirExist(const char *dir) {
  FILINFO fno;
  FRESULT res = f_stat(dir, &fno);

  // Check if the result is OK and if the attribute indicates it's a directory
  bool dirExist = (res == FR_OK && (fno.fattrib & AM_DIR));
  DPRINTF("Directory %s exists: %s\n", dir, dirExist ? "true" : "false");
  return dirExist;
}

sdcard_status_t sdcard_ensureFolder(const char *folderName) {
  if ((folderName == NULL) || (folderName[0] == '\0') ||
      (strcmp(folderName, "/") == 0)) {
    DPRINTF("Empty or root folder name. Ignoring.\n");
    return SDCARD_INIT_OK;
  }

  bool folderExists = sdcard_dirExist(folderName);
  DPRINTF("Folder exists: %s\n", folderExists ? "true" : "false");
  if (folderExists) {
    return SDCARD_INIT_OK;
  }

  FRESULT fres = f_mkdir(folderName);
  if (fres != FR_OK) {
    DPRINTF("Error creating the folder.\n");
    return SDCARD_CREATE_FOLDER_ERROR;
  }
  DPRINTF("Folder created.\n");
  return SDCARD_INIT_OK;
}

sdcard_status_t sdcard_initFilesystem(FATFS *fsPtr, const char *folderName) {
  sdMounted = false;
  mountedFsPtr = NULL;

  if ((fsPtr == NULL) || (folderName == NULL) || (folderName[0] == '\0')) {
    DPRINTF("Invalid SD filesystem initialization arguments.\n");
    return SDCARD_INIT_ERROR;
  }

  // Check the status of the sd card
  sdcard_status_t sdcardOk = sdcardInit();
  if (sdcardOk != SDCARD_INIT_OK) {
    DPRINTF("Error initializing the SD card.\n");
    return SDCARD_INIT_ERROR;
  }

  // Now try to mount the filesystem
  FRESULT fres;
  fres = sdcard_mountFilesystem(fsPtr, "0:");
  if (fres != FR_OK) {
    DPRINTF("Error mounting the filesystem.\n");
    return SDCARD_MOUNT_ERROR;
  }
  DPRINTF("Filesystem mounted.\n");

  sdcard_status_t folderStatus = sdcard_ensureFolder(folderName);
  if (folderStatus != SDCARD_INIT_OK) {
    return folderStatus;
  }

  mountedFsPtr = fsPtr;
  sdMounted = true;
  return SDCARD_INIT_OK;
}

void sdcard_changeSpiSpeed(int baudRateKbits) {
  size_t sdNum = sd_get_num();
  if (sdNum > 0) {
    int baudRate = baudRateKbits;
    if (baudRate > 0) {
      DPRINTF("Changing SD card baud rate to %i\n", baudRate);
      sd_card_t *sdCard = sd_get_by_num(sdNum - 1);
      if ((sdCard == NULL) || (sdCard->spi_if_p == NULL) ||
          (sdCard->spi_if_p->spi == NULL)) {
        DPRINTF("SD card SPI interface is not available\n");
        return;
      }
      sdCard->spi_if_p->spi->baud_rate = baudRate * SDCARD_KILOBAUD;
    } else {
      DPRINTF("Invalid baud rate. Using default value\n");
    }
  } else {
    DPRINTF("SD card not found\n");
  }
}

void sdcard_setSpiSpeedSettings() {
  // Get the SPI speed from the configuration
  SettingsConfigEntry *spiSpeed =
      settings_find_entry(gconfig_getContext(), PARAM_SD_BAUD_RATE_KB);
  int baudRate = 0;
  if (spiSpeed != NULL) {
    baudRate = atoi(spiSpeed->value);
  }

  // Clamp to a sane range; PARAM_SD_BAUD_RATE_KB is just a string in
  // shared config and a stale/typoed value (e.g. 999999) would otherwise
  // ask the SPI driver to clock past what the hardware sustains.
  if (baudRate > SDCARD_MAX_KHZ) {
    DPRINTF("Baud rate too high. Clamping to %d KHz\n", SDCARD_MAX_KHZ);
    baudRate = SDCARD_MAX_KHZ;
  }
  if (baudRate < SDCARD_MIN_KHZ) {
    DPRINTF("Baud rate too low. Clamping to %d KHz\n", SDCARD_MIN_KHZ);
    baudRate = SDCARD_MIN_KHZ;
  }

  sdcard_changeSpiSpeed(baudRate);
}

void sdcard_getInfo(FATFS *fsPtr, uint32_t *totalSizeMb,
                    uint32_t *freeSpaceMb) {
  if ((fsPtr == NULL) || (totalSizeMb == NULL) || (freeSpaceMb == NULL)) {
    DPRINTF("Invalid SD card info arguments.\n");
    return;
  }

  DWORD freClust;

  // Set initial values to zero as a precaution
  *totalSizeMb = 0;
  *freeSpaceMb = 0;

  // Get volume information and free clusters of drive
  FRESULT res = f_getfree("", &freClust, &fsPtr);
  if (res != FR_OK) {
    DPRINTF("Error getting free space information: %d\n", res);
    return;  // Error handling: Set values to zero if getfree fails
  }

  // Calculate total sectors in the SD card
  uint64_t totalSectors = (fsPtr->n_fatent - 2) * fsPtr->csize;

  // Convert total sectors to bytes and then to megabytes
  *totalSizeMb = (totalSectors * NUM_BYTES_PER_SECTOR) / SDCARD_MEGABYTE;

  // Convert free clusters to sectors and then to bytes
  uint64_t freeSpaceBytes =
      (uint64_t)freClust * fsPtr->csize * NUM_BYTES_PER_SECTOR;

  // Convert bytes to megabytes
  *freeSpaceMb = freeSpaceBytes / SDCARD_MEGABYTE;
}

bool sdcard_isMounted(void) { return sdMounted && (mountedFsPtr != NULL); }

bool sdcard_getMountedInfo(uint32_t *totalSizeMb, uint32_t *freeSpaceMb) {
  if ((totalSizeMb == NULL) || (freeSpaceMb == NULL)) {
    return false;
  }

  *totalSizeMb = 0;
  *freeSpaceMb = 0;

  if (!sdcard_isMounted()) {
    return false;
  }

  FATFS *fs = mountedFsPtr;
  DWORD freeClusters = 0;
  FRESULT res = f_getfree("", &freeClusters, &fs);
  if ((res != FR_OK) || (fs == NULL)) {
    DPRINTF("Error getting mounted free space information: %d\n", res);
    return false;
  }

  uint64_t totalSectors = (uint64_t)(fs->n_fatent - 2U) * fs->csize;
  *totalSizeMb =
      (uint32_t)((totalSectors * NUM_BYTES_PER_SECTOR) / SDCARD_MEGABYTE);

  uint64_t freeSpaceBytes =
      (uint64_t)freeClusters * fs->csize * NUM_BYTES_PER_SECTOR;
  *freeSpaceMb = (uint32_t)(freeSpaceBytes / SDCARD_MEGABYTE);
  return true;
}
