/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The mixer and the DMA helpers are ported from STDL - Atari ST DirectMedia
 * Layer (src/voice.c, src/audio.c and src/stram.c), Copyright (C) 2026 Neil
 * Rackett, LGPL-2.1-or-later: https://github.com/neilrackett/atarist-stdl
 */
/*
 * atari_sfx.c - ROTT's sound effects on DMA sound (STE, Mega STE) for the
 * native build (ROTT_ST.TOS): the FX_ calls rt_sound.c makes, in place of
 * the DOS sound library's fx_man.c, multivoc.c and its DMA driver.
 *
 * The DMA loops over a small mono ring at 12517Hz (6258Hz on a CPU that
 * measures as 8MHz, or as FXRate in sound.rot says), and a VBL routine mixes
 * forward in quarter-ring blocks (20ms each, 41 at 6258Hz) up to, but not into, the
 * quarter the hardware is playing, as STDL's voice mixer does. So sound
 * keeps going however long a frame takes, and a voice costs CPU only while
 * it plays: a byte fetch, a volume table fetch and a store (or an add) per
 * sample, with the resampling step split so the carry does the work.
 *
 * NumVoices in sound.rot sets how many sounds play at once, 1 to 8. With
 * every voice busy a new sound takes the one of lowest priority if its own
 * is as high, as the DOS library did, so one voice is one sound at a time.
 * 0 picks 2 with the ROTT Accelerator and 1 without (ATARI_SFX_Voices):
 * two mix straight into the ring, where a third takes the 16-bit sums
 * and the clamp, about twice the cost, on an ST busy with the game logic.
 *
 * ROTT's sounds are VOC files, unsigned 8-bit mono at about 11kHz: the
 * mixer resamples them as it goes, straight out of the cached lump, and the
 * volume table takes them to the signed samples the DMA plays.
 *
 * ROTT runs in user mode, so nothing here can mask interrupts, and nothing
 * needs to: the game only writes a voice while it is switched off (active
 * is the last field set), and the VBL only ever clears active, when a voice
 * ends. rt_sound.c hears about ended voices from the game side
 * (ATARI_SFX_Service), never from the VBL.
 */

#include <stdint.h>
#include <string.h>
#include <mint/osbind.h>
#include <mint/cookie.h>

#include "fx_man.h"
#include "pitch.h"
#include "atari_md.h"
#include "atari_megaste.h"
#include "atari_sfx.h"
#include "atari_vbl.h"

#define DMA_CTRL    (*(volatile uint8_t *)0xFFFF8901UL)
#define DMA_START_H (*(volatile uint8_t *)0xFFFF8903UL)
#define DMA_START_M (*(volatile uint8_t *)0xFFFF8905UL)
#define DMA_START_L (*(volatile uint8_t *)0xFFFF8907UL)
#define DMA_CNT_H   (*(volatile uint8_t *)0xFFFF8909UL)
#define DMA_CNT_M   (*(volatile uint8_t *)0xFFFF890BUL)
#define DMA_CNT_L   (*(volatile uint8_t *)0xFFFF890DUL)
#define DMA_END_H   (*(volatile uint8_t *)0xFFFF890FUL)
#define DMA_END_M   (*(volatile uint8_t *)0xFFFF8911UL)
#define DMA_END_L   (*(volatile uint8_t *)0xFFFF8913UL)
#define DMA_MODE    (*(volatile uint8_t *)0xFFFF8921UL)
#define MW_DATA     (*(volatile uint16_t *)0xFFFF8922UL)
#define MW_MASK     (*(volatile uint16_t *)0xFFFF8924UL)

/* Mono at 12517Hz, or 6258Hz on a CPU that measures as 8MHz (FX_Init):
 * half the mixing there, for duller sound. */
#define SFX_RATE_FULL 12517
#define SFX_RATE_HALF 6258
#define RING_FRAMES  1024               /* 82ms at 12517Hz, 164 at 6258 */
#define BLOCK_FRAMES (RING_FRAMES / 4)  /* a VBL's worth at 12517Hz; a multiple of 4 */
/* speed probe passes (atari_megaste.c) from which the full rate is used:
 * about 7400 at 8MHz, 15100 on a Mega STE at 16MHz with the cache */
#define SFX_FULL_RATE_PASSES 11000
static uint32_t sfx_rate = SFX_RATE_FULL;
static uint8_t sfx_mode = 0x80 | 1;     /* mono; 1 12517Hz, 0 6258Hz */
#define MAX_VOICES   8
#define VOL_LEVELS   65                 /* 0-64 */

/* the compiler must not move stores across these: see the top */
#define SFX_BARRIER() __asm__ volatile("" ::: "memory")

typedef struct
{
   /* the mixer's (STDL's voice_t) */
   const uint8_t *data;
   uint32_t pos;          /* 16.16 frame position                     */
   uint32_t end;          /* 16.16 end of the current stretch         */
   uint32_t loopstart;    /* 16.16 restart point after the first pass */
   uint32_t loopsize;     /* 16.16 loop length, 0 = one-shot          */
   uint32_t step;         /* 16.16 resampling step                    */
   const int8_t *vt;      /* volume table row                         */
   int16_t repeats;       /* loop passes left; -1 until stopped       */
   volatile uint8_t active;

   /* the game's */
   int handle;            /* 0: free, or ended and rt_sound.c told    */
   int priority;
   unsigned long callbackval;
   uint32_t rate;         /* the sound's own, for FX_SetPitch         */
} sfx_voice_t;

typedef struct
{
   const uint8_t *data;
   uint32_t len;
   uint32_t rate;
   int16_t repeats;
} sfx_sound_t;

static struct
{
   int open;
   int voices;            /* how many may play at once */
   int8_t *ring;
   void *ring_alloc;
   int fill_block;        /* next quarter to mix */
   uint8_t fresh;         /* first VBL after the start */
   uint8_t silent;        /* consecutive blocks mixed silent */
   uint8_t idle;          /* the ring all silence, the play head unwatched */
   uint32_t late;
   sfx_voice_t v[MAX_VOICES];
} mx;

static int8_t voltab[VOL_LEVELS * 256];
static int8_t clamp_tab[1024];
#define CLAMP_MID (clamp_tab + 512)
static int16_t mixacc[BLOCK_FRAMES];

int FX_ErrorCode = FX_Ok;
static void (*sfx_callback)(unsigned long);
static int sfx_volume = 255;
static int sfx_reverse;
static int sfx_next_handle = 1;

/* ------------------------------------------------------------------ */
/* the hardware (STDL's audio.c)                                      */

static __inline__ uint32_t sfx_mulu(uint16_t a, uint16_t b)
{
   uint32_t r = a;

   __asm__("mulu.w %1,%0" : "+d"(r) : "d"(b));
   return r;
}

/* a 32-bit value times a 16-bit one in two mulu.w, not __mulsi3 */
static __inline__ uint32_t sfx_mul32x16(uint32_t a, uint16_t b)
{
   return (sfx_mulu((uint16_t)(a >> 16), b) << 16) + sfx_mulu((uint16_t)a, b);
}

static __inline__ uint16_t sfx_divu(uint32_t a, uint16_t b)
{
   __asm__("divu.w %1,%0" : "+d"(a) : "d"(b));
   return (uint16_t)a;
}

static void microwire_write(uint16_t data)
{
   long timeout = 10000;

   MW_MASK = 0x07FF;
   MW_DATA = data;
   /* the mask rotates during the ~16us transfer; wait it out */
   while (MW_MASK != 0x07FF && --timeout > 0)
      ;
}

/* master and both channels to 0dB */
static void set_output_levels(void)
{
   microwire_write(0x4E8);
   microwire_write(0x554);
   microwire_write(0x514);
}

/*
 * The play position, read without tearing: three byte reads of a counter
 * moving underneath them, so a carry between two reads gives a value that
 * was never true, and that put the mixer into the block being played (a
 * click) on a real STE. If neither byte above the low one moved across
 * the read of it, the three belong together.
 */
static uint32_t dma_counter(void)
{
   uint8_t h, m, l, m2, h2;

   do
   {
      h = DMA_CNT_H;
      m = DMA_CNT_M;
      l = DMA_CNT_L;
      m2 = DMA_CNT_M;
      h2 = DMA_CNT_H;
   } while (h != h2 || m != m2);
   return ((uint32_t)h << 16) | ((uint32_t)m << 8) | l;
}

/* The DMA reads the ring directly, and cannot see alt-RAM. TOS 1.x has no
 * Mxalloc (EINVFN), but nor does it have any alt-RAM. */
static void *stram_alloc(long bytes)
{
   long p = (long)Mxalloc(bytes, 0);

   if (p < 0)
      p = (long)Malloc(bytes);
   return (p > 0) ? (void *)p : NULL;
}

/* ------------------------------------------------------------------ */
/* the mixer (STDL's voice.c)                                         */

enum { MIX_WSTORE, MIX_WADD, MIX_BSTORE, MIX_BADD };

/*
 * The inner loop by hand. The 16.16 position is split: the integer part in
 * a long whose upper word stays clear (frames are at most 65535) to index
 * the sample, the fraction in a word. add.w of the step's fraction sets X
 * on overflow and addx.w of its integer part takes the carry into the
 * index. Four samples to a pass, so the dbra is paid once in four; a run
 * that is not a multiple of four enters its first pass part-way in, the
 * jmp adding a body's size times the skip.
 */
#define VOICE_RUN_ASM(body)      \
   "moveq #0,%%d0\n\t"           \
   "jmp (1f,%%pc,%4.w)\n"        \
   "1:\n\t"                      \
   ".rept 4\n\t"                 \
   body                          \
   ".endr\n\t"                   \
   "dbra %3,1b"

/* %1 data, %2 the volume row, %5 index, %6 fraction, %7/%8 the step */
#define VR_STEP "add.w %8,%6\n\taddx.w %7,%5\n\t"
#define VR_W(store) "move.b (%1,%5.l),%%d0\n\t" \
   "move.b (%2,%%d0.w),%%d1\n\text.w %%d1\n\t" store " %%d1,(%0)+\n\t" VR_STEP
#define VR_BSTORE "move.b (%1,%5.l),%%d0\n\t" \
   "move.b (%2,%%d0.w),(%0)+\n\t" VR_STEP
#define VR_BADD "move.b (%1,%5.l),%%d0\n\t" \
   "move.b (%2,%%d0.w),%%d1\n\tadd.b %%d1,(%0)+\n\t" VR_STEP

#define VRS_ASM(body)                                                \
   __asm__ volatile(VOICE_RUN_ASM(body)                              \
      : "+a"(dst), "+a"(data), "+a"(vt), "+d"(count), "+d"(skip),    \
        "+d"(ipos), "+d"(fpos)                                       \
      : "d"(istep), "d"(fstep)                                       \
      : "d0", "d1", "cc", "memory")

/* bytes: the size of one body in the loop, for the part-way entry */
#define VR_SETUP(bytes)                                              \
   uint32_t ipos = pos >> 16;                                        \
   uint16_t fpos = (uint16_t)pos, fstep = (uint16_t)step;            \
   uint16_t istep = (uint16_t)(step >> 16);                          \
   int16_t count = (int16_t)(((run + 3) >> 2) - 1);                  \
   uint16_t skip = (uint16_t)((-run & 3) * (bytes))

/* run frames of one voice into the 16-bit sums */
static uint32_t vr_word(void *dst, const uint8_t *data, const int8_t *vt,
                        uint32_t pos, uint32_t step, int run, int mode)
{
   VR_SETUP(16);
   if (mode & 1)
      VRS_ASM(VR_W("add.w"));
   else
      VRS_ASM(VR_W("move.w"));
   return pos + sfx_mul32x16(step, (uint16_t)run);
}

/* run frames of one voice straight into the ring */
static uint32_t vr_byte(void *dst, const uint8_t *data, const int8_t *vt,
                        uint32_t pos, uint32_t step, int run, int mode)
{
   if (mode & 1)
   {
      VR_SETUP(14);
      VRS_ASM(VR_BADD);
   }
   else
   {
      VR_SETUP(12);
      VRS_ASM(VR_BSTORE);
   }
   return pos + sfx_mul32x16(step, (uint16_t)run);
}

/*
 * Frames a stretch has left, d (16.16) short of its end: ceil(d / step),
 * or a frame under it, never 0. d is under a block's worth of steps, so
 * the quotient fits a word and divu.w does it. A step past a word is
 * shifted down with d and rounded up, so the quotient can only come out
 * low, which costs one more pass round the stretch loop and nothing heard.
 */
static int frames_left(uint32_t d, uint32_t step)
{
   uint32_t n = d - 1;

   if (step > 0xFFFF)
   {
      do
      {
         step >>= 1;
         n >>= 1;
      } while (step > 0x7FFF);
      step++;
   }
   return sfx_divu(n, (uint16_t)step) + 1;
}

/* one voice through the block into dst (the sums or the ring, as mode
 * says), looping or ending as it goes */
static __inline__ __attribute__((always_inline))
void voice_mix(sfx_voice_t *v, int mode, void *dst, const int bytes)
{
   const int8_t *vt = v->vt;
   const uint8_t *data = v->data;
   uint32_t pos = v->pos, step = v->step, end = v->end;
   int n = 0;

   while (n < BLOCK_FRAMES)
   {
      int run = BLOCK_FRAMES - n;

      if (pos >= end)
      {
         uint32_t over = pos - end;

         if (v->loopsize == 0 || v->repeats == 0)
         {
            v->active = 0;
            if (mode == MIX_WSTORE)
               memset((int16_t *)dst + n, 0,
                      (BLOCK_FRAMES - n) * sizeof(int16_t));
            else if (mode == MIX_BSTORE)
               memset((int8_t *)dst + n, 0, BLOCK_FRAMES - n);
            break;
         }
         /* the overshoot carries into the loop, or a short loop comes
          * back late every time round and plays flat */
         if (over >= v->loopsize)
            over %= v->loopsize;
         if (v->repeats > 0)
            v->repeats--;
         pos = v->loopstart + over;
         end = v->loopstart + v->loopsize;
         v->end = end;
      }
      /* stop where pos reaches end: every frame reads inside */
      if (sfx_mul32x16(step, (uint16_t)(run - 1)) >= end - pos)
         run = frames_left(end - pos, step);
      if (bytes)
         pos = vr_byte((int8_t *)dst + n, data, vt, pos, step, run, mode);
      else
         pos = vr_word((int16_t *)dst + n, data, vt, pos, step, run, mode);
      n += run;
   }
   v->pos = pos;
}

static __attribute__((noinline)) void voice_mix_w(sfx_voice_t *v, int mode)
{
   voice_mix(v, mode, mixacc, 0);
}

static __attribute__((noinline)) void voice_mix_b(sfx_voice_t *v, int mode,
                                                  int8_t *dst)
{
   voice_mix(v, mode, dst, 1);
}

/*
 * Mix one quarter of the ring; returns 0 if it is silence. With one or two
 * voices each goes straight into the ring - two half-scale voices cannot
 * overflow a byte - and with more each adds into 16-bit sums that one
 * clamp table takes to the ring, so the sum is clamped once, not each
 * partial sum. Silence is only written until the whole ring holds it.
 */
static int mix_block(int8_t *dst)
{
   int active = 0, mode, i;

   for (i = 0; i < mx.voices; i++)
      active += mx.v[i].active;
   if (active == 0)
   {
      if (mx.silent < 4)
         memset(dst, 0, BLOCK_FRAMES);
      return 0;
   }
   if (active <= 2)
   {
      mode = MIX_BSTORE;
      for (i = 0; i < mx.voices; i++)
      {
         if (mx.v[i].active)
         {
            voice_mix_b(&mx.v[i], mode, dst);
            mode = MIX_BADD;
         }
      }
      return 1;
   }
   mode = MIX_WSTORE;
   for (i = 0; i < mx.voices; i++)
   {
      if (mx.v[i].active)
      {
         voice_mix_w(&mx.v[i], mode);
         mode = MIX_WADD;
      }
   }
   {
      /* four frames a pass; a block is a whole number of passes */
      const int16_t *acc = mixacc;
      const int8_t *mid = CLAMP_MID;
      int16_t count = BLOCK_FRAMES / 4 - 1;

      __asm__ volatile(
         "1:\n\t"
         ".rept 4\n\t"
         "move.w (%0)+,%%d0\n\t"
         "move.b (%1,%%d0.w),(%2)+\n\t"
         ".endr\n\t"
         "dbra %3,1b"
         : "+a"(acc), "+a"(mid), "+a"(dst), "+d"(count)
         :
         : "d0", "cc", "memory");
   }
   return 1;
}

/* the VBL queue routine: chase the play head, filling every quarter
 * behind it and stopping at the one the hardware is reading */
static void sfx_vbl(void)
{
   uint32_t off;
   int play_block, guard, i;

   if (!mx.open)
      return;
   /* Nothing playing and the whole ring already silence: nothing to mix,
    * so no play head to chase, which was most of the cost of a VBL. */
   if (mx.silent >= 4)
   {
      for (i = 0; i < mx.voices && !mx.v[i].active; i++)
         ;
      if (i == mx.voices)
      {
         mx.idle = 1;
         return;
      }
   }
   off = dma_counter() - (uint32_t)mx.ring;
   if (off >= (uint32_t)RING_FRAMES)
      return; /* counter mid-reload; next VBL */
   play_block = (int)(off / BLOCK_FRAMES);
   if (mx.idle)
   {
      /* The play head moved on while unwatched: start a sound in the next
       * block, not wherever the chase left off. The ring is all silence,
       * so whichever blocks get mixed, none is being played. */
      mx.idle = 0;
      mx.fill_block = (play_block + 1) & 3;
      mx.fresh = 1;
   }
   for (guard = 0; guard < 3; guard++)
   {
      if (mx.fill_block == play_block)
         break;
      if (mix_block(mx.ring + mx.fill_block * BLOCK_FRAMES))
         mx.silent = 0;
      else if (mx.silent < 4)
         mx.silent++;
      mx.fill_block = (mx.fill_block + 1) & 3;
   }
   /* Three blocks and still not caught up: this VBL came so late that the
    * hardware has replayed or half-read a block. Not on the first, which
    * fills three by construction (playback starts in block 0). */
   if (guard == 3 && !mx.fresh)
      mx.late++;
   mx.fresh = 0;
}

/* Supervisor: the DMA stopped (atari_vbl.c calls it on any way out). */
static void sfx_release(void)
{
   DMA_CTRL = 0;
}

/* Supervisor: the DMA looping over the ring. */
static long dma_start_super(void)
{
   uint32_t start = (uint32_t)mx.ring;
   uint32_t end = start + RING_FRAMES;

   /* stop first: the address registers latch into the counter when
    * playback starts, and writing them under a running DMA can be picked
    * up mid-frame */
   DMA_CTRL = 0;
   DMA_MODE = sfx_mode;
   DMA_START_H = (uint8_t)(start >> 16);
   DMA_START_M = (uint8_t)(start >> 8);
   DMA_START_L = (uint8_t)start;
   DMA_END_H = (uint8_t)(end >> 16);
   DMA_END_M = (uint8_t)(end >> 8);
   DMA_END_L = (uint8_t)end;
   set_output_levels();
   DMA_CTRL = 0x03; /* play, repeat */
   return 0;
}

static long dma_stop_super(void)
{
   sfx_release();
   return 0;
}

/* Volume rows: row[byte] = (byte - 128) * level / 128, the unsigned
 * sample to signed at half scale, so two voices sum to the full range;
 * rounded toward zero, as a floor is lopsided about zero and ticks in
 * quiet sounds. One voice can only ever be alone: full scale. */
static void build_tables(int full)
{
   const int shift = full ? 6 : 7;
   int level, s, i;

   for (i = -512; i < 512; i++)
      CLAMP_MID[i] = (int8_t)(i > 127 ? 127 : (i < -128 ? -128 : i));
   for (level = 0; level < VOL_LEVELS; level++)
   {
      int8_t *row = voltab + level * 256;

      for (s = 0; s < 256; s++)
      {
         int v = (s - 128) * level;

         v = (v < 0) ? -((-v) >> shift) : (v >> shift);
         row[s] = (int8_t)(v > 127 ? 127 : v);
      }
   }
}

/* ------------------------------------------------------------------ */
/* sounds                                                             */

static uint32_t le16(const uint8_t *p)
{
   return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
}

static uint32_t le24(const uint8_t *p)
{
   return le16(p) | ((uint32_t)p[2] << 16);
}

static uint32_t le32(const uint8_t *p)
{
   return le24(p) | ((uint32_t)p[3] << 24);
}

/* The first block of sound in a VOC file, 8-bit PCM mono, and whether a
 * repeat block wraps it: what ROTT's sounds are. */
static int decode_voc(const uint8_t *p, sfx_sound_t *s)
{
   uint32_t off = le16(p + 20);
   int16_t repeats = 0;
   int blocks;

   for (blocks = 0; blocks < 64; blocks++)
   {
      const uint8_t *b = p + off;
      uint32_t len = le24(b + 1);

      switch (b[0])
      {
      case 0:
         return -1; /* the end, and no sound */
      case 1:       /* time constant, codec */
         if (len < 3 || b[5] != 0)
            return -1;
         s->rate = 1000000UL / (256 - b[4]);
         s->data = b + 6;
         s->len = len - 2;
         s->repeats = repeats;
         return 0;
      case 9:       /* rate, bits, channels, codec, reserved */
         if (len < 13 || b[8] != 8 || b[9] != 1 || le16(b + 10) != 0)
            return -1;
         s->rate = le32(b + 4);
         s->data = b + 16;
         s->len = len - 12;
         s->repeats = repeats;
         return 0;
      case 6:       /* repeat: 0xFFFF until stopped */
         repeats = (le16(b + 4) == 0xFFFF) ? -1 : (int16_t)le16(b + 4);
         break;
      default:      /* text, markers */
         break;
      }
      off += 4 + len;
   }
   return -1;
}

/* 8-bit PCM mono WAV, which FX_PlayWAV3D's name promises */
static int decode_wav(const uint8_t *p, sfx_sound_t *s)
{
   const uint8_t *fmt = NULL;
   uint32_t riff_end = 8 + le32(p + 4);
   uint32_t off = 12;

   while (off + 8 <= riff_end)
   {
      uint32_t len = le32(p + off + 4);

      if (memcmp(p + off, "fmt ", 4) == 0)
         fmt = p + off + 8;
      else if (memcmp(p + off, "data", 4) == 0)
      {
         if (fmt == NULL || le16(fmt) != 1 || le16(fmt + 2) != 1 ||
             le16(fmt + 14) != 8)
            return -1;
         s->rate = le32(fmt + 4);
         s->data = p + off + 8;
         s->len = len;
         s->repeats = 0;
         return 0;
      }
      off += 8 + ((len + 1) & ~1UL);
   }
   return -1;
}

static int decode(const char *ptr, sfx_sound_t *s)
{
   const uint8_t *p = (const uint8_t *)ptr;
   int status = -1;

   if (p == NULL)
      return -1;
   if (memcmp(p, "Creative Voice File\032", 20) == 0)
      status = decode_voc(p, s);
   else if (memcmp(p, "RIFF", 4) == 0 && memcmp(p + 8, "WAVE", 4) == 0)
      status = decode_wav(p, s);
   if (status < 0 || s->len == 0 || s->rate == 0 || s->rate > 0xFFFF)
      return -1;
   if (s->len > 0xFFFF)
      s->len = 0xFFFF; /* 16.16 positions */
   return 0;
}

/* 16.16 frames of the sound per frame of the ring */
static uint32_t sfx_step(uint32_t rate, int pitchoffset)
{
   uint32_t step = (rate << 16) / sfx_rate;

   if (pitchoffset != 0)
      step = (step * (PITCH_GetScale(pitchoffset) >> 4)) >> 12;
   return step;
}

/* ROTT's 0-255 volume, times FX_SetVolume's, as a volume row (0-64) */
static int sfx_level(int vol)
{
   if (vol <= 0)
      return 0;
   if (vol > 255)
      vol = 255;
   return (vol * sfx_volume + 512) >> 10;
}

static int distance_volume(int distance)
{
   if (distance < 0)
      distance = -distance; /* behind: the same, as there is no stereo */
   return (distance >= 255) ? 0 : 255 - distance;
}

/* Stop a voice and tell rt_sound.c, once: it unlocks the lump. */
static void voice_release(sfx_voice_t *v)
{
   int handle = v->handle;

   v->active = 0;
   SFX_BARRIER();
   if (handle != 0)
   {
      v->handle = 0;
      if (sfx_callback != NULL)
         sfx_callback(v->callbackval);
   }
}

void ATARI_SFX_Service(void)
{
   int i;

   for (i = 0; i < mx.voices; i++)
   {
      if (mx.v[i].handle != 0 && !mx.v[i].active)
         voice_release(&mx.v[i]);
   }
}

/* A handle's low bits are its voice (sfx_start), so this is one look:
 * the game asks after each moving wall's sound every tic. */
static sfx_voice_t *sfx_find(int handle)
{
   sfx_voice_t *v;

   if (handle <= 0)
      return NULL;
   v = &mx.v[handle & (MAX_VOICES - 1)];
   return (v->handle == handle) ? v : NULL;
}

/* A free voice, or with take the lowest priority one if priority is as
 * high: the DOS library's rule. */
static sfx_voice_t *sfx_alloc(int priority, int take)
{
   sfx_voice_t *lowest = NULL;
   int i;

   if (!mx.open)
      return NULL;
   ATARI_SFX_Service();
   for (i = 0; i < mx.voices; i++)
   {
      if (mx.v[i].handle == 0)
         return &mx.v[i];
      if (lowest == NULL || mx.v[i].priority < lowest->priority)
         lowest = &mx.v[i];
   }
   if (lowest == NULL || lowest->priority > priority)
      return NULL;
   if (take)
      voice_release(lowest);
   return lowest;
}

static int sfx_start(const sfx_sound_t *s, int pitchoffset, int vol,
                     int priority, unsigned long callbackval)
{
   sfx_voice_t *v;
   int level = sfx_level(vol);

   if (!mx.open)
   {
      FX_ErrorCode = FX_SoundCardError;
      return FX_Warning;
   }
   /* inaudible: not worth a voice, and rt_sound.c unlocks the lump */
   if (level == 0 || (v = sfx_alloc(priority, 1)) == NULL)
   {
      FX_ErrorCode = FX_MultiVocError;
      return FX_Warning;
   }

   v->active = 0;
   SFX_BARRIER();
   v->data = s->data;
   v->pos = 0;
   v->end = s->len << 16;
   v->loopstart = 0;
   v->loopsize = s->repeats ? s->len << 16 : 0;
   v->repeats = s->repeats;
   v->rate = s->rate;
   v->step = sfx_step(s->rate, pitchoffset);
   v->vt = voltab + level * 256;
   v->priority = priority;
   v->callbackval = callbackval;
   v->handle = sfx_next_handle * MAX_VOICES + (int)(v - mx.v);
   if (++sfx_next_handle > 0x0FFF0000)
      sfx_next_handle = 1;
   SFX_BARRIER();
   v->active = 1;
   return v->handle;
}

static int sfx_play(const char *ptr, int pitchoffset, int vol, int priority,
                    unsigned long callbackval)
{
   sfx_sound_t s;

   if (decode(ptr, &s) < 0)
   {
      FX_ErrorCode = FX_MultiVocError;
      return FX_Warning;
   }
   return sfx_start(&s, pitchoffset, vol, priority, callbackval);
}

/* ------------------------------------------------------------------ */

int ATARI_SFX_HasDMA(void)
{
   static int present = -1;
   long cookie = 0;

   /* _SND bit 1: 8-bit DMA sound (bit 0 is the YM, which every ST has) */
   if (present < 0)
      present = (Getcookie(C__SND, &cookie) == C_FOUND && (cookie & 2)) ? 1 : 0;
   return present;
}

int ATARI_SFX_Voices(int numvoices)
{
   if (numvoices <= 0)
      numvoices = ATARI_MD_Active() ? 2 : 1;
   return (numvoices > MAX_VOICES) ? MAX_VOICES : numvoices;
}

void ATARI_SFX_GetDebugStats(int *playing, int *late)
{
   int i, n = 0;

   for (i = 0; i < mx.voices; i++)
      n += mx.v[i].active;
   if (playing)
      *playing = n;
   if (late)
      *late = (int)mx.late;
}

char *FX_ErrorString(int ErrorNumber)
{
   switch (ErrorNumber)
   {
   case FX_Warning:
   case FX_Error:
      if (FX_ErrorCode == FX_Warning || FX_ErrorCode == FX_Error)
         return "Unknown Fx error code.";
      return FX_ErrorString(FX_ErrorCode);
   case FX_Ok:
      return "Fx ok.";
   case FX_SoundCardError:
      return "No DMA sound: sound effects need an STE or Mega STE.";
   case FX_MultiVocError:
      return "Sound not played.";
   default:
      return "Unknown Fx error code.";
   }
}

int FX_SetupCard(int SoundCard, fx_device *device)
{
   (void)SoundCard;
   if (!ATARI_SFX_HasDMA())
   {
      FX_ErrorCode = FX_SoundCardError;
      return FX_Error;
   }
   device->MaxVoices = MAX_VOICES;
   device->MaxSampleBits = 8;
   device->MaxChannels = 1;
   return FX_Ok;
}

int FX_Init(int SoundCard, int numvoices, int numchannels, int samplebits,
            unsigned mixrate)
{
   (void)SoundCard;
   (void)numchannels;
   (void)samplebits;
   (void)mixrate;

   if (mx.open)
      FX_Shutdown();
   if (!ATARI_SFX_HasDMA())
   {
      FX_ErrorCode = FX_SoundCardError;
      return FX_Error;
   }
   memset(&mx, 0, sizeof(mx));
   mx.voices = (numvoices < 1) ? 1 : (numvoices > MAX_VOICES ? MAX_VOICES : numvoices);

   /* The rate: FXRate in sound.rot, or measured (the Mega STE switched to
    * 16MHz first, as it would be by now in play). */
   if (AtariFXRate > 0)
      sfx_rate = (AtariFXRate < (SFX_RATE_HALF + SFX_RATE_FULL) / 2) ?
                 SFX_RATE_HALF : SFX_RATE_FULL;
   else
   {
      if (is_megaste())
         megaste_enable_16mhz_cache();
      cpu_speed_measure();
      sfx_rate = (cpu_speed_passes >= SFX_FULL_RATE_PASSES) ?
                 SFX_RATE_FULL : SFX_RATE_HALF;
   }
   sfx_mode = (uint8_t)(0x80 | (sfx_rate == SFX_RATE_FULL ? 1 : 0));

   /* guard bytes past the ring, all cleared: a real STE clicked once a
    * loop on a silent ring with uninitialised bytes after it, so whatever
    * the DMA reads at or just past the end address is a zero */
   mx.ring_alloc = stram_alloc(RING_FRAMES + 8);
   if (mx.ring_alloc == NULL)
   {
      FX_ErrorCode = FX_SoundCardError;
      return FX_Error;
   }
   memset(mx.ring_alloc, 0, RING_FRAMES + 8);
   mx.ring = (int8_t *)(((uintptr_t)mx.ring_alloc + 1) & ~(uintptr_t)1);
   build_tables(mx.voices == 1);

   mx.fill_block = 1; /* playback starts in block 0 */
   mx.fresh = 1;
   mx.open = 1;
   Supexec(dma_start_super);
   if (ATARI_VBL_Add(sfx_vbl, sfx_release) < 0)
   {
      Supexec(dma_stop_super);
      mx.open = 0;
      Mfree(mx.ring_alloc);
      mx.ring_alloc = NULL;
      FX_ErrorCode = FX_SoundCardError;
      return FX_Error;
   }
   return FX_Ok;
}

int FX_Shutdown(void)
{
   int i;

   if (!mx.open)
      return FX_Ok;
   for (i = 0; i < mx.voices; i++)
      voice_release(&mx.v[i]);
   ATARI_VBL_Remove(sfx_vbl); /* and stops the DMA */
   mx.open = 0;
   Mfree(mx.ring_alloc);
   mx.ring_alloc = NULL;
   mx.ring = NULL;
   return FX_Ok;
}

int FX_SetCallBack(void (*function)(unsigned long))
{
   sfx_callback = function;
   return FX_Ok;
}

/* For sounds started from now on. */
void FX_SetVolume(int volume)
{
   sfx_volume = (volume < 0) ? 0 : (volume > 255 ? 255 : volume);
}

int FX_GetVolume(void)
{
   return sfx_volume;
}

/* Mono: kept only so the setting reads back. */
void FX_SetReverseStereo(int setting)
{
   sfx_reverse = setting;
}

int FX_GetReverseStereo(void)
{
   return sfx_reverse;
}

/* No reverb: ROTT asks for it by the size of the room. */
void FX_SetReverb(int reverb)
{
   (void)reverb;
}

void FX_SetFastReverb(int reverb)
{
   (void)reverb;
}

int FX_GetMaxReverbDelay(void)
{
   return 0;
}

int FX_GetReverbDelay(void)
{
   return 0;
}

void FX_SetReverbDelay(int delay)
{
   (void)delay;
}

int FX_VoiceAvailable(int priority)
{
   return sfx_alloc(priority, 0) != NULL;
}

int FX_EndLooping(int handle)
{
   sfx_voice_t *v = sfx_find(handle);

   if (v == NULL)
      return FX_Warning;
   v->repeats = 0;
   return FX_Ok;
}

int FX_SetPan(int handle, int vol, int left, int right)
{
   sfx_voice_t *v = sfx_find(handle);

   (void)left;
   (void)right;
   if (v == NULL)
      return FX_Warning;
   v->vt = voltab + sfx_level(vol) * 256;
   return FX_Ok;
}

int FX_SetPitch(int handle, int pitchoffset)
{
   sfx_voice_t *v = sfx_find(handle);

   if (v == NULL)
      return FX_Warning;
   v->step = sfx_step(v->rate, pitchoffset);
   return FX_Ok;
}

int FX_SetFrequency(int handle, int frequency)
{
   sfx_voice_t *v = sfx_find(handle);

   if (v == NULL || frequency <= 0 || frequency > 0xFFFF)
      return FX_Warning;
   v->rate = (uint32_t)frequency;
   v->step = sfx_step(v->rate, 0);
   return FX_Ok;
}

int FX_PlayVOC(char *ptr, int pitchoffset, int vol, int left, int right,
               int priority, unsigned long callbackval)
{
   (void)left;
   (void)right;
   return sfx_play(ptr, pitchoffset, vol, priority, callbackval);
}

int FX_PlayWAV(char *ptr, int pitchoffset, int vol, int left, int right,
               int priority, unsigned long callbackval)
{
   (void)left;
   (void)right;
   return sfx_play(ptr, pitchoffset, vol, priority, callbackval);
}

/* Not used by ROTT. */
int FX_PlayLoopedVOC(char *ptr, long loopstart, long loopend,
                     int pitchoffset, int vol, int left, int right,
                     int priority, unsigned long callbackval)
{
   (void)ptr; (void)loopstart; (void)loopend; (void)pitchoffset; (void)vol;
   (void)left; (void)right; (void)priority; (void)callbackval;
   FX_ErrorCode = FX_MultiVocError;
   return FX_Warning;
}

int FX_PlayLoopedWAV(char *ptr, long loopstart, long loopend,
                     int pitchoffset, int vol, int left, int right,
                     int priority, unsigned long callbackval)
{
   return FX_PlayLoopedVOC(ptr, loopstart, loopend, pitchoffset, vol, left,
                           right, priority, callbackval);
}

int FX_PlayVOC3D(char *ptr, int pitchoffset, int angle, int distance,
                 int priority, unsigned long callbackval)
{
   (void)angle;
   return sfx_play(ptr, pitchoffset, distance_volume(distance), priority,
                   callbackval);
}

/* What rt_sound.c calls for every sound: a VOC or a WAV. */
int FX_PlayWAV3D(char *ptr, int pitchoffset, int angle, int distance,
                 int priority, unsigned long callbackval)
{
   return FX_PlayVOC3D(ptr, pitchoffset, angle, distance, priority,
                       callbackval);
}

int FX_PlayVOC3D_ROTT(char *ptr, int size, int pitchoffset, int angle,
                      int distance, int priority, unsigned long callbackval)
{
   (void)size;
   return FX_PlayVOC3D(ptr, pitchoffset, angle, distance, priority,
                       callbackval);
}

int FX_PlayWAV3D_ROTT(char *ptr, int size, int pitchoffset, int angle,
                      int distance, int priority, unsigned long callbackval)
{
   (void)size;
   return FX_PlayVOC3D(ptr, pitchoffset, angle, distance, priority,
                       callbackval);
}

int FX_PlayRaw(char *ptr, unsigned long length, unsigned rate,
               int pitchoffset, int vol, int left, int right, int priority,
               unsigned long callbackval)
{
   sfx_sound_t s;

   (void)left;
   (void)right;
   if (ptr == NULL || length == 0 || rate == 0 || rate > 0xFFFF)
   {
      FX_ErrorCode = FX_MultiVocError;
      return FX_Warning;
   }
   s.data = (const uint8_t *)ptr;
   s.len = (length > 0xFFFF) ? 0xFFFF : length;
   s.rate = rate;
   s.repeats = 0;
   return sfx_start(&s, pitchoffset, vol, priority, callbackval);
}

/* Not used by ROTT. */
int FX_PlayLoopedRaw(char *ptr, unsigned long length, char *loopstart,
                     char *loopend, unsigned rate, int pitchoffset, int vol,
                     int left, int right, int priority,
                     unsigned long callbackval)
{
   (void)ptr; (void)length; (void)loopstart; (void)loopend; (void)rate;
   (void)pitchoffset; (void)vol; (void)left; (void)right; (void)priority;
   (void)callbackval;
   FX_ErrorCode = FX_MultiVocError;
   return FX_Warning;
}

int FX_Pan3D(int handle, int angle, int distance)
{
   sfx_voice_t *v = sfx_find(handle);

   (void)angle;
   if (v == NULL)
      return FX_Warning;
   v->vt = voltab + sfx_level(distance_volume(distance)) * 256;
   return FX_Ok;
}

int FX_SoundActive(int handle)
{
   sfx_voice_t *v = sfx_find(handle);

   if (v == NULL)
      return 0;
   if (!v->active)
   {
      voice_release(v); /* ended: as ATARI_SFX_Service would */
      return 0;
   }
   return 1;
}

int FX_SoundsPlaying(void)
{
   int playing;

   ATARI_SFX_Service();
   ATARI_SFX_GetDebugStats(&playing, NULL);
   return playing;
}

int FX_StopSound(int handle)
{
   sfx_voice_t *v = sfx_find(handle);

   if (v == NULL)
   {
      FX_ErrorCode = FX_MultiVocError;
      return FX_Warning;
   }
   voice_release(v);
   return FX_Ok;
}

int FX_StopAllSounds(void)
{
   int i;

   for (i = 0; i < mx.voices; i++)
      voice_release(&mx.v[i]);
   return FX_Ok;
}

/* Remote ridicule over a modem: not here. */
int FX_StartDemandFeedPlayback(void (*function)(char **ptr, unsigned long *length),
                               int rate, int pitchoffset, int vol, int left,
                               int right, int priority,
                               unsigned long callbackval)
{
   (void)function; (void)rate; (void)pitchoffset; (void)vol; (void)left;
   (void)right; (void)priority; (void)callbackval;
   FX_ErrorCode = FX_MultiVocError;
   return FX_Warning;
}

int FX_StartRecording(int MixRate, void (*function)(char *ptr, int length))
{
   (void)MixRate;
   (void)function;
   FX_ErrorCode = FX_MultiVocError;
   return FX_Warning;
}

void FX_StopRecord(void)
{
}
