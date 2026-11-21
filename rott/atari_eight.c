/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The mode set, the monitor tests and the way back to the desktop follow
 * STDL - Atari ST DirectMedia Layer (src/responsive.c, src/video.c),
 * Copyright (C) 2026 Neil Rackett, LGPL-2.1-or-later, which has them from
 * real TTs and Hatari's Falcon: https://github.com/neilrackett/atarist-stdl
 * The conversion is STDL's own (lib/stdl/addons/sdl2/src/c2p.c), built
 * into the game as it is.
 */
/*
 * atari_eight.c - 256 colours on a TT or a Falcon (atari_eight.h).
 *
 * One page of the display's own, in ST RAM: TT Low (320x480, each row on
 * two lines) or a Falcon's 8-bit mode (320x240 on VGA, the VIDEL doubling
 * the lines; 320x200 on RGB or a TV, at the desktop's PAL or NTSC). The
 * game's 320x200 sits in the middle of 240 rows. Drawn as ST Low is, with
 * no second page: atari_c2p.c converts what changed of the chunky screen,
 * and the Accelerator's view is copied in as it comes (atari_md.c).
 *
 * The colours are the game's own, written in the vertical blank
 * (atari_vbl.c), whose release also puts TOS's mode, screen and colours
 * back on any exit.
 */

#include <string.h>
#include <mint/osbind.h>
#include <mint/cookie.h>
#include <mint/falcon.h>

#include "atari_eight.h"

#if ATARI_EIGHT

#include "atari_vbl.h"
#include "../lib/stdl/addons/sdl2/src/c2p.h"

#ifndef ATARI_NOIR
#define ATARI_NOIR 0
#endif

#define ST_PALETTE     ((volatile unsigned short *)0xFFFF8240UL)
#define TT_PALETTE     ((volatile unsigned short *)0xFFFF8400UL) /* 0000RRRR GGGGBBBB */
#define FALCON_PALETTE ((volatile unsigned long *)0xFFFF9800UL)  /* RRRRRR00 GGGGGG00 00000000 BBBBBB00 */
#define MFP_GPIP       (*(volatile unsigned char *)0xFFFFFA01UL)
#define DMA_CTRL       (*(volatile unsigned char *)0xFFFF8901UL)
#define FRCLOCK        (*(volatile unsigned long *)0x466UL)
#define PHYSTOP        (*(volatile unsigned long *)0x42EUL)

/* VsetMode's word, and VgetMonitor's answers */
#define VM_BPS8     0x0003
#define VM_VGA      0x0010
#define VM_PAL      0x0020
#define VM_VERTFLAG 0x0100
#define MONITOR_MONO 0
#define MONITOR_VGA  2

#define MACHINE_TT     2 /* _VDO's high word */
#define MACHINE_FALCON 3

#define LINE_BYTES 320L /* 320 pixels in 8 planes */
#define ROWS       200

int atari_eight;
int atari_eight_colours = 256;
long atari_eight_second;

static int machine;
static int prepared;
static int mode_up;
static long page_bytes;
static unsigned char *page;
static void *block; /* the page's allocation */
static unsigned char *row_at[ROWS];

static short old_rez, old_vmode, new_vmode;
static void *old_log, *old_phys;
static unsigned short old_st_palette[16];
static unsigned long old_palette[256];

/* The colours given, and as shown: mixed towards fade_target by
 * fade_amount sixteenths (atari_c2p_fade's rule). */
static unsigned char installed[256 * 3], shown[256 * 3];
static unsigned char fade_target[3];
static int fade_amount;

/* The registers' words, written in the blank. */
static unsigned long pending[256];
static volatile unsigned char pending_dirty;
static int vbl_up;

/* ---------------------------------------------------------------- */
/* the machine                                                      */

/* A TT's monitor: MFP GPIP 7 is high with a colour one. The DMA sound's
 * play line is XORed into that bit, so it comes back out. */
static long tt_colour_super(void)
{
   return (((MFP_GPIP >> 7) ^ DMA_CTRL) & 1) != 0;
}

static long phystop_super(void)
{
   return (long)PHYSTOP;
}

static long save_super(void)
{
   int i;

   for (i = 0; i < 16; i++)
      old_st_palette[i] = ST_PALETTE[i];
   for (i = 0; i < 256; i++)
      old_palette[i] = machine == MACHINE_FALCON ? FALCON_PALETTE[i] : TT_PALETTE[i];
   return 0;
}

static void put_registers(const unsigned long *words)
{
   int i;

   if (machine == MACHINE_FALCON)
   {
      volatile unsigned long *r = FALCON_PALETTE;

      for (i = 0; i < 256; i++)
         *r++ = *words++;
   }
   else
   {
      volatile unsigned short *r = TT_PALETTE;

      for (i = 0; i < 256; i++)
         *r++ = (unsigned short)*words++;
   }
}

static long put_pending_super(void)
{
   put_registers(pending);
   return 0;
}

/* The vertical blank: new colours, if any. */
static void eight_vbl(void)
{
   if (pending_dirty)
   {
      put_registers(pending);
      pending_dirty = 0;
   }
}

/*
 * TOS's mode, screen and colours back. Supervisor, hardware and the XBIOS
 * only: it also runs from the terminate vector (atari_vbl.c). The colours
 * go after a blank, which lands any the XBIOS still holds for the mode
 * set; the wait is bounded, as the counter stands still if the VBL is
 * masked at exit.
 */
static void eight_release(void)
{
   unsigned long fc;
   long n;
   int i;

   if (!mode_up)
      return;
   mode_up = 0;
   pending_dirty = 0;
   if (machine == MACHINE_FALCON)
      (void)VsetScreen(old_log, old_phys, 3, old_vmode);
   else
      (void)Setscreen(old_log, old_phys, old_rez);
   fc = FRCLOCK;
   for (n = 0; n < 400000L && FRCLOCK == fc; n++)
   {
   }
   if (machine == MACHINE_FALCON)
   {
      for (i = 0; i < 16; i++)
         ST_PALETTE[i] = old_st_palette[i];
   }
   put_registers(old_palette);
}

static long release_super(void)
{
   eight_release();
   return 0;
}

int atari_eight_prepare(void)
{
   long vdo = 0, lines, p;

   if (prepared)
      return 1;
   if (atari_eight_colours < 256)
      return 0;
   if (Getcookie(C__VDO, &vdo) != C_FOUND)
      return 0;
   machine = (int)(vdo >> 16);
   if (machine == MACHINE_TT)
   {
      /* its colour monitor shows ST High too: the monitor, not the mode */
      if (!Supexec(tt_colour_super))
         return 0;
      lines = 480;
      atari_eight_second = LINE_BYTES;
   }
   else if (machine == MACHINE_FALCON)
   {
      const int monitor = VgetMonitor();

      if (monitor == MONITOR_MONO)
         return 0;
      if (monitor == MONITOR_VGA)
      {
         lines = 240;
         new_vmode = VM_BPS8 | VM_VGA | VM_VERTFLAG;
      }
      else
      {
         /* the desktop's PAL or NTSC: a set made for one may not sync to
          * the other */
         lines = 200;
         new_vmode = (short)(VM_BPS8 | (VsetMode(-1) & VM_PAL));
      }
      atari_eight_second = 0;
   }
   else
   {
      return 0;
   }

   /* The display reads ST RAM alone, and Malloc gives a program TT RAM
    * where there is some. Asked for, then checked. */
   page_bytes = lines * LINE_BYTES;
   p = (long)Mxalloc(page_bytes + 256, 0);
   if (p <= 0)
      return 0;
   if ((unsigned long)p + page_bytes + 256 > (unsigned long)Supexec(phystop_super))
   {
      Mfree((void *)p);
      return 0;
   }
   block = (void *)p;
   page = (unsigned char *)(((unsigned long)p + 255) & ~255UL);
   {
      /* 200 rows in the middle of the display's */
      const long step = LINE_BYTES << (atari_eight_second != 0);
      unsigned char *r = page + (((lines / (step / LINE_BYTES)) - ROWS) >> 1) * step;
      int y;

      for (y = 0; y < ROWS; y++, r += step)
         row_at[y] = r;
   }
   prepared = 1;
   return 1;
}

int atari_eight_wanted(void)
{
   return prepared;
}

/* Not once the screen is up. */
void atari_eight_cancel(void)
{
   if (!prepared || mode_up)
      return;
   prepared = 0;
   Mfree(block);
   block = NULL;
}

static void stage(void);

int atari_eight_enter(void)
{
   if (!prepared || mode_up)
      return atari_eight;
   old_rez = Getrez();
   old_log = Logbase();
   old_phys = Physbase();
   if (machine == MACHINE_FALCON)
      old_vmode = VsetMode(-1);
   Supexec(save_super);
   memset(page, 0, (size_t)page_bytes);
   /* a resolution change: the console is reset with it, and TOS sets
    * colours of its own, on a Falcon a blank or two later */
   if (machine == MACHINE_FALCON)
      (void)VsetScreen(page, page, 3, new_vmode);
   else
      (void)Setscreen(page, page, 7);
   Vsync();
   Vsync();
   memset(page, 0, (size_t)page_bytes);
   mode_up = 1;
   atari_eight = 1;
   fade_amount = 0;
   memset(installed, 0, sizeof(installed));
   memset(shown, 0, sizeof(shown));
   vbl_up = ATARI_VBL_Add(eight_vbl, eight_release) == 0;
   stage();
   return 1;
}

void atari_eight_leave(void)
{
   if (!atari_eight)
      return;
   atari_eight = 0;
   if (vbl_up)
   {
      vbl_up = 0;
      ATARI_VBL_Remove(eight_vbl); /* and its release */
   }
   else
   {
      Supexec(release_super);
   }
}

/* ---------------------------------------------------------------- */
/* the colours                                                      */

/* `shown` as the registers' words, for the next blank. */
static void stage(void)
{
   const unsigned char *c = shown;
   int i;

   pending_dirty = 0;
   if (machine == MACHINE_FALCON)
   {
      for (i = 0; i < 256; i++, c += 3)
         pending[i] = ((unsigned long)(c[0] & 0xFC) << 24) |
                      ((unsigned long)(c[1] & 0xFC) << 16) | (unsigned long)(c[2] & 0xFC);
   }
   else
   {
      for (i = 0; i < 256; i++, c += 3)
         pending[i] = (unsigned long)(((c[0] >> 4) << 8) | (c[1] & 0xF0) | (c[2] >> 4));
   }
   if (vbl_up)
      pending_dirty = 1;
   else
      Supexec(put_pending_super);
}

/* a towards b by amount sixteenths */
static unsigned char mix(unsigned char a, unsigned char b, int amount)
{
   const short x = (short)a, y = (short)b, m = (short)amount;

   return (unsigned char)((x * (short)(16 - m) + y * m + 8) >> 4);
}

static void mix_all(unsigned char *out, const unsigned char *from, const unsigned char *to,
                    int amount)
{
   int i;

   for (i = 0; i < 256 * 3; i++)
      out[i] = mix(from[i], to[i], amount);
}

static void mix_target(unsigned char *out, int amount)
{
   int i;

   for (i = 0; i < 256 * 3; i += 3)
   {
      out[i] = mix(installed[i], fade_target[0], amount);
      out[i + 1] = mix(installed[i + 1], fade_target[1], amount);
      out[i + 2] = mix(installed[i + 2], fade_target[2], amount);
   }
}

void atari_eight_set_palette(const unsigned char *colors)
{
   if (ATARI_NOIR)
   {
      int i;

      for (i = 0; i < 256 * 3; i += 3)
      {
         const unsigned short luma =
             (unsigned short)(colors[i] * 77u + colors[i + 1] * 150u + colors[i + 2] * 29u);

         installed[i] = installed[i + 1] = installed[i + 2] = (unsigned char)(luma >> 8);
      }
   }
   else
   {
      memcpy(installed, colors, sizeof(installed));
   }
   if (fade_amount)
      mix_target(shown, fade_amount);
   else
      memcpy(shown, installed, sizeof(shown));
   stage();
}

void atari_eight_fade(int r, int g, int b, int amount, int vbls)
{
   static unsigned char from[256 * 3], to[256 * 3];
   int k;

   if (amount < 0)
      amount = 0;
   if (amount > 16)
      amount = 16;
   fade_target[0] = (unsigned char)r;
   fade_target[1] = (unsigned char)g;
   fade_target[2] = (unsigned char)b;
   fade_amount = amount;
   memcpy(from, shown, sizeof(from));
   mix_target(to, amount);
   /* from what is on screen now, whatever mix that was, to the new one */
   for (k = 1; k < vbls; k++)
   {
      mix_all(shown, from, to, (16 * k) / vbls);
      stage();
      Vsync();
   }
   memcpy(shown, to, sizeof(shown));
   stage();
   if (vbls > 0)
      Vsync();
}

int atari_eight_fade_amount(void)
{
   return fade_amount;
}

const unsigned char *atari_eight_colors(void)
{
   return installed;
}

/* ---------------------------------------------------------------- */
/* the picture                                                      */

unsigned char *atari_eight_row(int y)
{
   return row_at[y];
}

void atari_eight_convert(const unsigned char *in, int x, int y, int w, int h)
{
   if (w < 16 || h <= 0)
      return;
   stdl_sdl2_c2p(in + ((long)y << 8) + ((long)y << 6) + x, 320, row_at[y] + x, LINE_BYTES,
                 (long)(w >> 4), (long)h, atari_eight_second != 0, &stdl_sdl2_c2p_plain, 0, 0);
}

void atari_eight_convert_row(const unsigned char *row, int y)
{
   stdl_sdl2_c2p(row, 320, row_at[y], LINE_BYTES, 20, 1, atari_eight_second != 0,
                 &stdl_sdl2_c2p_plain, 0, 0);
}

void atari_eight_convert_span(const unsigned char *row, int x, int w, int y)
{
   if (w < 16)
      return;
   stdl_sdl2_c2p(row + x, 320, row_at[y] + x, LINE_BYTES, (long)(w >> 4), 1,
                 atari_eight_second != 0, &stdl_sdl2_c2p_plain, 0, 0);
}

void atari_eight_convert_pairs(const unsigned char *row, int x, int w, int y)
{
   extern stdl_sdl2_c2p_fn atari_c2pp1, atari_c2pp2;

   if (w < 16)
      return;
   (atari_eight_second ? atari_c2pp2 : atari_c2pp1)(row + x, 320, row_at[y] + x, LINE_BYTES,
                                                    (long)(w >> 4), 1, 0, 0);
}

/* rows y to y + h of pairs, groups x to x + w */
void atari_eight_convert_pairs_box(const unsigned char *in, int x, int w, int y, int h)
{
   extern stdl_sdl2_c2p_fn atari_c2pp1, atari_c2pp2;

   if (w < 16 || h <= 0)
      return;
   (atari_eight_second ? atari_c2pp2 : atari_c2pp1)(in + ((long)y << 8) + ((long)y << 6) + x, 320,
                                                    row_at[y] + x, LINE_BYTES, (long)(w >> 4), (long)h, 0, 0);
}

void atari_eight_copy_row(int from, int to)
{
   memcpy(row_at[to], row_at[from], (size_t)(LINE_BYTES << (atari_eight_second != 0)));
}

void atari_eight_to_chunky(unsigned char *chunky, int x, int y, int w, int h)
{
   int row, col, p;

   for (row = y; row < y + h; row++)
   {
      unsigned char *out = chunky + ((long)row << 8) + ((long)row << 6);

      for (col = x; col < x + w; col++)
      {
         const unsigned short *group = (const unsigned short *)(row_at[row] + (col & ~15));
         const int bit = 15 - (col & 15);
         int c = 0;

         for (p = 7; p >= 0; p--)
            c = (c << 1) | ((group[p] >> bit) & 1);
         out[col] = (unsigned char)c;
      }
   }
}

#endif /* ATARI_EIGHT */
