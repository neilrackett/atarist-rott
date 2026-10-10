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

#if !defined(YMMUSIC_MD)
// The native build's own MUSIC_ call (atari_music_api.c): the song in a
// WAD lump, on the ROTT Accelerator if it can; MUSIC_Ok if so.
int MUSIC_PlaySongLump(int lump, int loopflag);
#endif

#define YMMUSIC_MAX_MIDI_TRACKS 32

// A MIDI track: its data from start to end, read at ptr.
typedef struct
{
    unsigned long next; /* song tick of the track's next event; all ones once ended */
    unsigned char *start;
    unsigned char *ptr;
    unsigned char *end;
    unsigned char running_status;
    unsigned char active;
} ymmusic_midi_track_t;

#if defined(YMMUSIC_MD)
// The ROTT Accelerator's firmware (sidecart/rp/src/md_music.c), which
// streams each track through a window: the player plays a track's next
// event only once its window holds enough of it, and on a loop waits for
// the windows to go back to the tracks' starts (1 when they have).
int YMMUSIC_TRACK_READY(const ymmusic_midi_track_t *t);
int ymmusic_md_rewind(void);

// The registers after the last step: R0-R13 (R7 without its port bits).
extern unsigned char ymmusic_regs[14];

// Stop: silence, and the song, voices and tracks reset. Then point the
// tracks (ymmusic_md_track) at their data, and begin a MIDI song.
void ymmusic_md_stop(void);
ymmusic_midi_track_t *ymmusic_md_track(int i);
void ymmusic_md_begin(unsigned short division, int tracks, int loop);
void ymmusic_md_pause(int paused);
int ymmusic_md_playing(void);
#else
#define YMMUSIC_TRACK_READY(t) 1
#endif

#endif
