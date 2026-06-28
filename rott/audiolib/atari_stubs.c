/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stddef.h>

unsigned long DisableInterrupts( void )
{
    return 0;
}

void RestoreInterrupts( unsigned long flags )
{
    (void)flags;
}

void mixer_callback( void *stream, int len )
{
    (void)stream;
    (void)len;
}

void EnableScreenStretch(void) { }
void DisableScreenStretch(void) { }
int iG_playerTilt = 0;
