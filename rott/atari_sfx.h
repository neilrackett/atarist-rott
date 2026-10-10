/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
/*
 * atari_sfx.h - ROTT's sound effects on DMA sound (STE, Mega STE), for the
 * native build: the FX_ calls in fx_man.h, plus these. See atari_sfx.c.
 */
#ifndef ATARI_SFX_H
#define ATARI_SFX_H

/* The machine has 8-bit DMA sound. */
int ATARI_SFX_HasDMA(void);

/* How many sounds play at once for NumVoices in sound.rot: 1-8 as set,
 * or 0 for automatic (2 with the ROTT Accelerator, 1 without). */
int ATARI_SFX_Voices(int numvoices);

/* FXRate in sound.rot (rt_cfg.c): 0 automatic (half the rate, and half the
 * mixing, on a CPU that measures as 8MHz), or the rate wanted in Hz. */
extern int AtariFXRate;

/* Once a frame: tells rt_sound.c about sounds that have ended, so their
 * lumps can be purged again. The FX_ calls do it too. */
void ATARI_SFX_Service(void);

/* For the Help key's overlay: sounds playing, and mixer refills that came too
 * late to stay ahead of the DMA (each one a repeated or torn block). */
void ATARI_SFX_GetDebugStats(int *playing, int *late);

#endif
