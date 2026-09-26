/* Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* Host stand-in for the FatFs calls md_pack.c makes, over stdio. Paths are
 * relative to the emulated SD card (mdemu_init). */
#ifndef EMU_FF_H
#define EMU_FF_H
#include "pico.h"
typedef enum { FR_OK = 0, FR_DISK_ERR, FR_NO_FILE } FRESULT;
typedef unsigned int UINT;
typedef uint32_t FSIZE_t;
typedef struct {
  FILE *fp;
  FSIZE_t size;
} FIL;
typedef struct {
  int unused;
} FATFS;
#define FA_READ 0x01
FRESULT f_open(FIL *fp, const char *path, int mode);
FRESULT f_read(FIL *fp, void *buff, UINT btr, UINT *br);
FRESULT f_lseek(FIL *fp, FSIZE_t ofs);
FRESULT f_close(FIL *fp);
#define f_size(fp) ((fp)->size)
#endif
