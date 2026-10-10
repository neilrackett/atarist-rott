/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
/*
 * The MUSIC_ calls for the native build: ROTT's MIDI songs on the YM2149
 * (atari_music.c), stepped at 50Hz from the VBL in supervisor mode, so the
 * tempo holds however long a frame takes. While music is on, TOS's key
 * click and bell are off, so they cannot fight over the chip, as STDL's
 * ym.c does (https://github.com/neilrackett/atarist-stdl).
 */
#include <string.h>
#include <mint/osbind.h>

#include "rt_def.h"
#include "music.h"
#include "atari_music.h"
#include "atari_vbl.h"

#define CONTERM (*(volatile unsigned char *)0x484UL)
#define HZ_200 (*(volatile unsigned long *)0x4BAUL)

int MUSIC_ErrorCode = MUSIC_Ok;

static int music_initialized = 0;
static int music_loopflag = MUSIC_PlayOnce;
static int music_volume = 196;
static int music_context = 0;
static unsigned long music_ticks = 0;
static unsigned long music_ms = 0;
static unsigned long music_hz200 = 0;
static int old_conterm = -1;

void MUSIC_Service(void);

static void music_set_error(int code)
{
    MUSIC_ErrorCode = code;
}

static void music_send_command(unsigned char *song, unsigned short state)
{
    ++ymmusic_cmd_nr_begin;
    ymmusic_data_cmd = song;
    ymmusic_state_cmd = state;
    ++ymmusic_cmd_nr_end;
}

char *MUSIC_ErrorString(int ErrorNumber)
{
    switch (ErrorNumber)
    {
    case MUSIC_Warning:
    case MUSIC_Error:
        return MUSIC_ErrorString(MUSIC_ErrorCode);
    case MUSIC_Ok:
        return "Music ok.";
    case MUSIC_InvalidCard:
        return "Invalid music device.";
    case MUSIC_MidiError:
        return "Unsupported/invalid song format.";
    default:
        return "Unknown music error.";
    }
}

/* The VBL routine: a step every 4 ticks of the 200Hz clock, so 50 a
 * second whatever the VBL rate, and no burst after interrupts were off. */
static void music_vbl(void)
{
    unsigned long now = HZ_200;
    int steps = 0;

    if (music_hz200 == 0)
        music_hz200 = now;
    while ((now - music_hz200) >= 4)
    {
        if (++steps > 4)
        {
            music_hz200 = now;
            break;
        }
        MUSIC_Service();
        music_hz200 += 4;
    }
}

/* Supervisor: the YM silent and TOS's key click and bell back, here or
 * from the terminate vector (atari_vbl.c). */
static void music_release(void)
{
    ymmusic_silence();
    if (old_conterm >= 0)
    {
        CONTERM = (unsigned char)old_conterm;
        old_conterm = -1;
    }
}

static long music_init_super(void)
{
    ymmusic_init();
    old_conterm = CONTERM;
    CONTERM = (unsigned char)(old_conterm & ~5); /* key click, bell */
    return 0;
}

static long music_release_super(void)
{
    music_release();
    return 0;
}

int MUSIC_Init(int SoundCard, int Address)
{
    (void)SoundCard;
    (void)Address;
    if (music_initialized)
        MUSIC_Shutdown();
    music_ticks = 0;
    music_ms = 0;
    music_hz200 = 0;
    music_send_command(NULL, 0);
    Supexec(music_init_super);
    if (ATARI_VBL_Add(music_vbl, music_release) < 0)
    {
        Supexec(music_release_super);
        music_set_error(MUSIC_Error);
        return MUSIC_Error;
    }
    music_initialized = 1;
    music_set_error(MUSIC_Ok);
    return MUSIC_Ok;
}

int MUSIC_Shutdown(void)
{
    if (!music_initialized)
        return MUSIC_Ok;
    music_initialized = 0;
    ATARI_VBL_Remove(music_vbl); /* and silences the YM */
    music_ticks = 0;
    music_ms = 0;
    return MUSIC_Ok;
}

void MUSIC_SetMaxFMMidiChannel(int channel)
{
    (void)channel;
}

void MUSIC_SetVolume(int volume)
{
    if (volume < 0)
        volume = 0;
    if (volume > 255)
        volume = 255;

    music_volume = volume;
    ymmusic_master = volume;
}

void MUSIC_SetMidiChannelVolume(int channel, int volume)
{
    (void)channel;
    (void)volume;
}

void MUSIC_ResetMidiChannelVolumes(void)
{
}

int MUSIC_GetVolume(void)
{
    return music_volume;
}

void MUSIC_SetLoopFlag(int loopflag)
{
    music_loopflag = loopflag;
}

int MUSIC_SongPlaying(void)
{
    if (!music_initialized)
        return 0;
    return (ymmusic_state & YMMUSIC_PLAY) ? 1 : 0;
}

void MUSIC_Continue(void)
{
    if (!music_initialized || ymmusic_data_cmd == NULL)
        return;

    music_send_command(ymmusic_data_cmd, YMMUSIC_PLAY | ((music_loopflag == MUSIC_LoopSong) ? YMMUSIC_LOOP : 0));
}

void MUSIC_Pause(void)
{
    if (!music_initialized)
        return;
    music_send_command(ymmusic_data_cmd, ymmusic_state_cmd & ~YMMUSIC_PLAY);
}

int MUSIC_StopSong(void)
{
    if (!music_initialized)
        return MUSIC_Ok;

    music_send_command(NULL, 0);
    music_ticks = 0;
    music_ms = 0;
    return MUSIC_Ok;
}

int MUSIC_PlaySong(unsigned char *song, int loopflag)
{
    (void)song;
    (void)loopflag;
    music_set_error(MUSIC_MidiError);
    return MUSIC_Error;
}

int MUSIC_PlaySongROTT(unsigned char *song, int size, int loopflag)
{
    (void)size;

    if (!music_initialized)
    {
        music_set_error(MUSIC_Error);
        return MUSIC_Error;
    }

    if (song == NULL)
    {
        music_set_error(MUSIC_MidiError);
        return MUSIC_Error;
    }

    if (!((song[0] == 'M' && song[1] == 'U' && song[2] == 'S' && song[3] == 0x1a) ||
          (song[0] == 'M' && song[1] == 'T' && song[2] == 'h' && song[3] == 'd')))
    {
        music_set_error(MUSIC_MidiError);
        return MUSIC_Error;
    }

    music_loopflag = loopflag;
    music_ticks = 0;
    music_ms = 0;
    music_set_error(MUSIC_Ok);

    music_send_command(song, YMMUSIC_PLAY | ((loopflag == MUSIC_LoopSong) ? YMMUSIC_LOOP : 0));
    return MUSIC_Ok;
}

void MUSIC_SetContext(int context)
{
    music_context = context;
}

int MUSIC_GetContext(void)
{
    return music_context;
}

void MUSIC_SetSongTick(unsigned long PositionInTicks)
{
    music_ticks = PositionInTicks;
}

void MUSIC_SetSongTime(unsigned long milliseconds)
{
    music_ms = milliseconds;
}

void MUSIC_SetSongPosition(int measure, int beat, int tick)
{
    (void)measure;
    (void)beat;
    (void)tick;
}

void MUSIC_GetSongPosition(songposition *pos)
{
    if (pos == NULL)
        return;

    memset(pos, 0, sizeof(*pos));
    pos->tickposition = music_ticks;
    pos->milliseconds = music_ms;
}

void MUSIC_GetSongLength(songposition *pos)
{
    if (pos == NULL)
        return;

    memset(pos, 0, sizeof(*pos));
}

int MUSIC_FadeVolume(int tovolume, int milliseconds)
{
    (void)milliseconds;
    MUSIC_SetVolume(tovolume);
    return MUSIC_Ok;
}

int MUSIC_FadeActive(void)
{
    return 0;
}

void MUSIC_StopFade(void)
{
}

void MUSIC_RerouteMidiChannel(int channel, int cdecl (*function)(int event, int c1, int c2))
{
    (void)channel;
    (void)function;
}

void MUSIC_RegisterTimbreBank(unsigned char *timbres)
{
    (void)timbres;
}

/* One 50Hz step, from the VBL. A command not yet taken (a new song, a
 * stop, a pause) still needs one, or a stopped song would hold its notes. */
void MUSIC_Service(void)
{
    if (!music_initialized || !ymmusic_active())
        return;
    ymmusic_update();
    if (ymmusic_state & YMMUSIC_PLAY)
    {
        ++music_ticks;
        music_ms += 20;
    }
}
