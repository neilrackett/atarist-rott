#include <string.h>

#include "rt_def.h"
#include "music.h"
#include "atari_music.h"

int MUSIC_ErrorCode = MUSIC_Ok;
int snd_MusicVolume = 15;

static int music_initialized = 0;
static int music_loopflag = MUSIC_PlayOnce;
static int music_volume = 196;
static int music_context = 0;
static unsigned long music_ticks = 0;
static unsigned long music_ms = 0;

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

int MUSIC_Init(int SoundCard, int Address)
{
    (void)SoundCard;
    (void)Address;

    ymmusic_init();
    music_initialized = 1;
    music_ticks = 0;
    music_ms = 0;
    music_set_error(MUSIC_Ok);
    music_send_command(NULL, 0);
    return MUSIC_Ok;
}

int MUSIC_Shutdown(void)
{
    if (!music_initialized)
        return MUSIC_Ok;

    music_send_command(NULL, 0);
    music_initialized = 0;
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
    snd_MusicVolume = (volume * 15) / 255;
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

void MUSIC_Service(void)
{
    if (!music_initialized)
        return;

    if (!(ymmusic_state_cmd & YMMUSIC_PLAY))
        return;

    ymmusic_update();
    ++music_ticks;
    music_ms += 20;
}
