/**
 * File: debug.h
 * Author: Diego Parrilla Santamaría
 * Date: July 2023, February 2026
 * Copyright: 2023-2026 - GOODDATA LABS SL
 * Description: Header file for basic traces and debug messages
 */

#ifndef DEBUG_H
#define DEBUG_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "constants.h"
#include "pico/stdlib.h"

/**
 * @brief A macro to print debug
 *
 * @param fmt The format string for the debug message, similar to printf.
 * @param ... Variadic arguments corresponding to the format specifiers in the
 * fmt parameter.
 */
#if defined(_DEBUG) && (_DEBUG != 0)
#define DPRINTF(fmt, ...)                                               \
  do {                                                                  \
    const char *file =                                                  \
        strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__; \
    /* printf, not fprintf(stderr): the SDK substitutes its own compact \
     * printf for the former, while the latter drags in newlib's, and    \
     * with it the floating-point formatter -- about 14 KB of flash the  \
     * debug build has not got. Both land on the same UART. */           \
    printf("%s:%d:%s(): " fmt "", file, __LINE__, __func__,             \
           ##__VA_ARGS__);                                              \
  } while (0)
#define DPRINTFRAW(fmt, ...)             \
  do {                                   \
    printf(fmt, ##__VA_ARGS__);          \
  } while (0)

/**
 * @brief Report the heap window (end..__StackLimit) and how much of it
 * the C runtime consumed before the caller ran. Boot-time settings init
 * needs ~8.4 KB of it; a shortfall fails its mallocs and the app bails
 * to Booster, so main() probes this just before gconfig_init.
 */
#define DPRINT_HEAP()                                                    \
  do {                                                                   \
    extern char end, __StackLimit;                                       \
    void *brk = malloc(4);                                               \
    DPRINTF("Heap: window %d B (%p..%p), break at %p, ~%d B consumed\n", \
            (int)(&__StackLimit - &end), (void *)&end,                   \
            (void *)&__StackLimit, brk, (int)((char *)brk - &end));      \
    free(brk);                                                           \
  } while (0)

#else
#define DPRINTF(fmt, ...)
#define DPRINTFRAW(fmt, ...)
#define DPRINT_HEAP()
#endif

#endif  // DEBUG_H
