/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef AUDIOLIB_ATARI_MUSIC_H
#define AUDIOLIB_ATARI_MUSIC_H

#define YMMUSIC_PLAY 1
#define YMMUSIC_LOOP 2
#define YMMUSIC_STOP 4

// Set by the game thread when a music change is requested.
extern unsigned char *ymmusic_data_cmd;
extern unsigned short ymmusic_state_cmd;

// Incremented before making any changes (to ensure atomicity).
extern unsigned short ymmusic_cmd_nr_begin;
// Incremented after making any changes (to ensure atomicity).
extern unsigned short ymmusic_cmd_nr_end;

// Actual state.
extern unsigned short ymmusic_state;
// Music volume, 0-255.
extern int ymmusic_master;

// Called to initialize tables, and silence the YM (supervisor).
extern void ymmusic_init();
// Called at 50Hz (supervisor) to drive playback and push YM-2149 updates.
extern void ymmusic_update();
// Every voice silent and off, keeping the song and its place (supervisor).
extern void ymmusic_silence();
// A command waiting, or a song playing.
extern int ymmusic_active();

#endif
