/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Ported from STDL - Atari ST DirectMedia Layer (src/vbl.c, and the
 * terminate vector handling in src/video.c), Copyright (C) 2026 Neil
 * Rackett, LGPL-2.1-or-later: https://github.com/neilrackett/atarist-stdl
 */
/*
 * atari_vbl.c - routines on the vertical blank for the native build (the
 * sound effects mixer and the music), behind one TOS VBL queue slot.
 *
 * GEMDOS gives a program's memory away when it ends, whether through
 * QuitGame, Error() or a crash, and a queue slot still pointing into it
 * crashes the machine a frame later, while the hardware a routine drives
 * (the DMA reading its ring, the YM holding a note) carries on regardless.
 * So the slot and each routine's release go through the GEMDOS terminate
 * vector as well as ATARI_VBL_Remove.
 */

#include <stddef.h>
#include <mint/osbind.h>

#include "atari_vbl.h"

#define NVBLS        (*(volatile unsigned short *)0x454UL)
#define VBLQUEUE     (*(void (***)(void))0x456UL)

#define MAX_ROUTINES 4

static struct
{
   void (*volatile fn)(void);
   void (*release)(void);
} routines[MAX_ROUTINES];

static int slot = -1;
static void (*old_term)(void);
static void (*releasing)(void);

/* The queue entry. TOS saves the registers around it. */
static void vbl_dispatch(void)
{
   int i;

   for (i = 0; i < MAX_ROUTINES; i++)
   {
      void (*fn)(void) = routines[i].fn;

      if (fn != NULL)
         fn();
   }
}

/* Supervisor. */
static void slot_clear(void)
{
   if (slot >= 0)
   {
      if (VBLQUEUE[slot] == vbl_dispatch)
         VBLQUEUE[slot] = NULL;
      slot = -1;
   }
}

static long slot_install_super(void)
{
   unsigned short sr;
   int i, n = NVBLS;

   if (n > 16)
      n = 16;
   /* a slot is a long: the VBL must not land between its two words */
   __asm__ volatile("move.w %%sr,%0\n\tori.w #0x0700,%%sr" : "=d"(sr) : : "cc");
   for (i = 0; i < n; i++)
   {
      if (VBLQUEUE[i] == NULL)
      {
         VBLQUEUE[i] = vbl_dispatch;
         slot = i;
         break;
      }
   }
   __asm__ volatile("move.w %0,%%sr" : : "d"(sr) : "cc");
   return (slot >= 0) ? 0 : -1;
}

static long slot_remove_super(void)
{
   slot_clear();
   return 0;
}

static long release_super(void)
{
   if (releasing != NULL)
      releasing();
   return 0;
}

/* Pterm: the slot first, then each routine's hardware. */
static void vbl_term(void)
{
   int i;

   (void)Setexc(0x102, (void *)old_term);
   slot_clear();
   for (i = 0; i < MAX_ROUTINES; i++)
   {
      void (*release)(void) = routines[i].release;

      routines[i].fn = NULL;
      routines[i].release = NULL;
      if (release != NULL)
         release();
   }
   if (old_term != NULL)
      old_term();
}

int ATARI_VBL_Add(void (*fn)(void), void (*release)(void))
{
   int i, free_slot = -1;

   for (i = 0; i < MAX_ROUTINES; i++)
   {
      if (routines[i].fn == fn)
         return 0;
      if (routines[i].fn == NULL && free_slot < 0)
         free_slot = i;
   }
   if (fn == NULL || free_slot < 0)
      return -1;
   if (slot < 0)
   {
      if (Supexec(slot_install_super) < 0)
         return -1;
      old_term = (void (*)(void))Setexc(0x102, (void *)vbl_term);
   }
   routines[free_slot].release = release;
   routines[free_slot].fn = fn; /* last: the VBL may call it from here on */
   return 0;
}

void ATARI_VBL_Remove(void (*fn)(void))
{
   int i, left = 0;

   for (i = 0; i < MAX_ROUTINES; i++)
   {
      if (fn != NULL && routines[i].fn == fn)
      {
         routines[i].fn = NULL;
         releasing = routines[i].release;
         routines[i].release = NULL;
         if (releasing != NULL)
            Supexec(release_super);
         releasing = NULL;
      }
      left |= (routines[i].fn != NULL);
   }
   if (!left && slot >= 0)
   {
      Supexec(slot_remove_super);
      (void)Setexc(0x102, (void *)old_term);
   }
}
