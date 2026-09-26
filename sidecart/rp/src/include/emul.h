/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * File: emul.h
 * Description: MD/ROTT boot sequence and main loop (emul.c).
 */

#ifndef EMUL_H
#define EMUL_H

/* Bring up the cartridge emulation and serve the ST. Never returns. */
void emul_start(void);

#endif  // EMUL_H
