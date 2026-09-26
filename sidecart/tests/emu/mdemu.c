/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * mdemu.c - host glue for the MD/ROTT firmware emulator (see mdemu.h).
 */
#include "mdemu.h"

#include <stdlib.h>

#include "commemul.h"
#include "constants.h"
#include "debug.h"
#include "ff.h"
#include "hardware/flash.h"
#include "md_core1.h"
#include "md_main.h"
#include "md_proto.h"
#include "rott/r_local.h"
#include "rott_md_protocol.h"
#include "target_firmware.h"

int emu_verbose;

/* ---- the RP's memory ---------------------------------------------- */

static uint16_t s_rom4[0x8000] __attribute__((aligned(8)));
uint8_t emu_flash[EMU_FLASH_BYTES];

/* Real flash wants whole sectors and pages; catch any other use here. */
void flash_range_erase(uint32_t offset, size_t count) {
  if ((offset | count) & (FLASH_SECTOR_SIZE - 1u)) {
    fprintf(stderr, "mdemu: unaligned flash erase %x+%zx\n", offset, count);
    abort();
  }
  if (offset + count <= EMU_FLASH_BYTES) memset(emu_flash + offset, 0xFF, count);
}

void flash_range_program(uint32_t offset, const uint8_t *data, size_t count) {
  if ((offset | count) & (FLASH_PAGE_SIZE - 1u)) {
    fprintf(stderr, "mdemu: unaligned flash program %x+%zx\n", offset, count);
    abort();
  }
  if (offset + count <= EMU_FLASH_BYTES) {
    for (size_t i = 0; i < count; i++) emu_flash[offset + i] &= data[i];
  }
}

/* ---- clock ---------------------------------------------------------- */

static emu_timer_hw_t s_timer;
emu_timer_hw_t *timer_hw = &s_timer;

uint32_t time_us_32(void) { return s_timer.timerawl; }
void mdemu_set_time_us(uint32_t us) { s_timer.timerawl = us; }

/* ---- the ROM3 capture ring and its interrupt -------------------------- */

static uint16_t s_ring[4096];
static unsigned s_ring_head, s_ring_tail;
static irq_handler_t s_irq;

void commemul_set_irq_handler(irq_handler_t handler) { s_irq = handler; }
void commemul_irq_ack(void) {}
void commemul_poll(CommEmulSampleCallback cb) {
  while (s_ring_tail != s_ring_head) {
    cb(s_ring[s_ring_tail]);
    s_ring_tail = (s_ring_tail + 1) & 4095u;
  }
}

/* ---- Core 1: jobs just run ------------------------------------------ */

static md_core1_job_t s_job;
static void *s_job_arg;
void md_core1_dispatch(md_core1_job_t job, void *arg) {
  s_job = job;
  s_job_arg = arg;
}
void md_core1_wait(void) {
  if (s_job) s_job(s_job_arg);
  s_job = NULL;
}
void md_core1_park(void) {}
void md_core1_unpark(void) {}

/* ---- the SD card ---------------------------------------------------- */

static char s_sd_root[512];

FRESULT f_open(FIL *fp, const char *path, int mode) {
  char full[1024];
  (void)mode;
  snprintf(full, sizeof(full), "%s%s", s_sd_root, path);
  fp->fp = fopen(full, "rb");
  if (!fp->fp) return FR_NO_FILE;
  fseek(fp->fp, 0, SEEK_END);
  fp->size = (FSIZE_t)ftell(fp->fp);
  fseek(fp->fp, 0, SEEK_SET);
  return FR_OK;
}
FRESULT f_read(FIL *fp, void *buff, UINT btr, UINT *br) {
  *br = (UINT)fread(buff, 1, btr, fp->fp);
  return FR_OK;
}
FRESULT f_lseek(FIL *fp, FSIZE_t ofs) {
  return fseek(fp->fp, (long)ofs, SEEK_SET) ? FR_DISK_ERR : FR_OK;
}
FRESULT f_close(FIL *fp) {
  fclose(fp->fp);
  return FR_OK;
}

/* ---- frame dumps ------------------------------------------------------ */

static const char *s_dump_dir;
static int s_dump_every;
static uint16_t s_last_dumped;

void mdemu_dump_frames(const char *dir, int every) {
  s_dump_dir = dir;
  s_dump_every = every > 0 ? every : 1;
}

/* The published planar frame, decoded back through the 16 ST colours, so
 * the dump shows exactly what the ST would copy to its screen. */
static void dump_ready_frame(void) {
  const uint16_t *st = s_rom4 + MD_STATUS_OFFSET / 2;
  const uint16_t seq = st[MD_ST_READY_SEQ];
  const uint16_t frames = st[MD_ST_FRAMES];
  if (!s_dump_dir || frames == s_last_dumped || (frames % s_dump_every)) return;
  s_last_dumped = frames;
  const unsigned w = st[MD_ST_VIEW_W], h = st[MD_ST_VIEW_H];
  const uint16_t *pal = s_rom4 + MD_PALETTE_OFFSET / 2;
  const uint16_t *fb =
      s_rom4 + (st[MD_ST_READY_BUF] ? MD_FRAME_OFFSET_B : MD_FRAME_OFFSET_A) / 2;
  char name[1024];
  snprintf(name, sizeof(name), "%s/md%05u.ppm", s_dump_dir, seq);
  FILE *f = fopen(name, "wb");
  if (!f) return;
  fprintf(f, "P6\n%u %u\n255\n", w, h);
  for (unsigned y = 0; y < h; y++) {
    for (unsigned x = 0; x < w; x++) {
      const uint16_t *g = fb + y * (w / 4) + (x / 16) * 4;
      const unsigned bit = 15 - (x & 15);
      unsigned pen = 0;
      for (unsigned p = 0; p < 4; p++) pen |= ((g[p] >> bit) & 1u) << p;
      const uint16_t c = pal[pen];
      /* ST colour word: 0000 rRRR gGGG bBBB with the STE low bit on top */
      unsigned rgb[3];
      for (int k = 0; k < 3; k++) {
        const unsigned n = (c >> (8 - 4 * k)) & 15u;
        const unsigned v = ((n & 7u) << 1) | (n >> 3);
        rgb[k] = v * 17u;
      }
      fputc((int)rgb[0], f);
      fputc((int)rgb[1], f);
      fputc((int)rgb[2], f);
    }
  }
  fclose(f);
}

/* ---- the cartridge port --------------------------------------------- */

int mdemu_init(const char *sd_root) {
  snprintf(s_sd_root, sizeof(s_sd_root), "%s", sd_root ? sd_root : ".");
  emu_verbose = getenv("MDEMU_VERBOSE") != NULL;
  memset(s_rom4, 0, sizeof(s_rom4));
  memset(emu_flash, 0xFF, sizeof(emu_flash));
  /* target_firmware[] holds the cartridge image as ST word values. */
  for (unsigned i = 0; i < target_firmware_length && i < 0x8000; i++)
    s_rom4[i] = target_firmware[i];
  md_proto_init((uintptr_t)s_rom4);
  md_main_init((uintptr_t)s_rom4, "/rott", true);
  md_main_ready();
  return 0;
}

uint16_t mdemu_rom4_word(uint32_t offset) {
  /* The real main loop never stops; here it runs when the ST touches the
   * cartridge. Polling ROM4 (waiting for a frame, say) gives it idle
   * turns: heartbeat, and md_pack_service between frames. */
  static unsigned reads;
  if ((++reads & 255u) == 0) {
    while (md_main_poll()) dump_ready_frame();
  }
  return s_rom4[(offset & 0xFFFFu) >> 1];
}

void mdemu_rom3_read(uint32_t addr16) {
  s_ring[s_ring_head] = (uint16_t)addr16;
  s_ring_head = (s_ring_head + 1) & 4095u;
  if (s_irq) s_irq();
  while (md_main_poll()) {
    dump_ready_frame();
  }
}
