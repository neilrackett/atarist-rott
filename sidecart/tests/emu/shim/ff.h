/* Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* Host stand-in for the FatFs calls the firmware makes (md_pack.c reading
 * the WAD, md_main.c's WAD copy writing it), over stdio. Paths are relative
 * to the emulated SD card (mdemu_init). */
#ifndef EMU_FF_H
#define EMU_FF_H
#include "pico.h"
typedef enum { FR_OK = 0, FR_DISK_ERR, FR_NO_FILE } FRESULT;
typedef unsigned int UINT;
typedef uint32_t DWORD;
typedef uint32_t FSIZE_t;
typedef struct {
  FILE *fp;
  FSIZE_t size;
} FIL;
typedef struct {
  DWORD csize; /* sectors a cluster */
} FATFS;
#define FA_READ 0x01
#define FA_WRITE 0x02
#define FA_CREATE_ALWAYS 0x08
FRESULT f_open(FIL *fp, const char *path, int mode);
FRESULT f_read(FIL *fp, void *buff, UINT btr, UINT *br);
FRESULT f_write(FIL *fp, const void *buff, UINT btw, UINT *bw);
FRESULT f_lseek(FIL *fp, FSIZE_t ofs);
FRESULT f_close(FIL *fp);
FRESULT f_unlink(const char *path);
FRESULT f_rename(const char *path_old, const char *path_new);
FRESULT f_mkdir(const char *path);
FRESULT f_getfree(const char *path, DWORD *nclst, FATFS **fatfs);
#define f_size(fp) ((fp)->size)
#endif
