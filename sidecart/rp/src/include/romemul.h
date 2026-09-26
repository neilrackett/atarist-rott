/**
 * File: romemul.h
 * Author: Diego Parrilla Santamaría
 * Date: July 2023-2025, February 2026
 * Copyright: 2023-2026 - GOODDATA LABS SL
 * Description: Header file for the ROM emulator C program.
 */

#ifndef ROMEMUL_H
#define ROMEMUL_H

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>

#include "constants.h"
#include "debug.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "hardware/structs/bus_ctrl.h"
#include "hardware/vreg.h"
#include "memfunc.h"
#include "pico/stdlib.h"
#include "romemul.pio.h"

#define ROMEMUL_BUS_BITS 16

/* Initialise the ROM emulator engine. ROM4 reads are served entirely
 * by chained DMAs feeding the PIO TX FIFO -- no CPU/IRQ involvement.
 *
 * @param copyFlashToRAM   If true, copy the cart image from flash
 *                         to ROM_IN_RAM at boot. emul.c does its
 *                         own erase + copy so it passes false here.
 * @return PIO state-machine index on success, -1 on DMA/PIO failure. */
int init_romemul(bool copyFlashToRAM);

#endif  // ROMEMUL_H
