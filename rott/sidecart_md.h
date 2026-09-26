/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
/*
 * sidecart_md.h - SidecarTridge Multi-device client (MD/ROTT).
 *
 * From atarist-stdoom's sidecart_md.c/h: finds the firmware, sends commands
 * over the ROM3 protocol (sidecart_stubs.S) and reads its ROM4 window,
 * keeping the Mega STE cache out of the way. The command ids and layout
 * come from sidecart/include/rott_md_protocol.h; nothing here is specific
 * to ROTT beyond that.
 */

#ifndef SIDECART_MD_H
#define SIDECART_MD_H

#include "rott_md_protocol.h"

#define MD_ROM4(offset) ((volatile unsigned char *)(MD_ROM4_BASE + (offset)))
#define MD_STATUS ((volatile unsigned short *)(MD_ROM4_BASE + MD_STATUS_OFFSET))

/* Token-wait loop count used by the stubs; about 40 cycles a loop. */
extern long md_command_timeout;
#define MD_TIMEOUT_DETECT 0x00040000L /* ~1 s at 8 MHz, first contact */
#define MD_TIMEOUT_NORMAL 0x00002000L /* ~40 ms at 8 MHz              */

/* True if the ready magic is in ROM4, i.e. some MD/ROTT firmware is up.
 * Cheap: one byte read, no command. */
int sidecart_md_present(void);

/* Sync command: d3, d4 (both sent). Returns 0 or -1 after the retries. */
int sidecart_md_command(int cmd, long d3, long d4);

/* Write command: d3, d4, d5 and `bytes` of `buf` (even address, at most
 * MD_CMD_MAX_BYTES). Returns 0 or -1 after the retries. */
int sidecart_md_write(int cmd, const void *buf, int bytes, long d3, long d4,
                      long d5);

/* The firmware's result text (version or error). */
void sidecart_md_result(char *buf, int size);

/* Bracket every stretch of cartridge-bus traffic. On a Mega STE with its
 * cache on, the cache can hold stale ROM3/ROM4 reads, so it is switched
 * off (16 MHz kept) between begin and end. Nests. Cheap elsewhere. */
void sidecart_md_bus_begin(void);
void sidecart_md_bus_end(void);

/* Mask 68000 interrupts around each command (supervisor only; off by
 * default -- the firmware tolerates the delays an interrupt causes). */
void sidecart_md_set_intr_mask(int enable);

/* Commands that failed after their retries, for the debug line. */
extern unsigned short md_command_failures;

#endif /* SIDECART_MD_H */
