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

// Called to initialize tables.
extern void ymmusic_init();
// Called cyclically to drive internal playback state and push YM-2149 updates.
extern void ymmusic_update();

#endif
