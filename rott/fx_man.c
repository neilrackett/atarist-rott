/*
 * A reimplementation of Jim Dose's FX_MAN routines using SDL_mixer.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "SDL.h"
#include "SDL_mixer.h"

#include "rt_def.h"
#include "fx_man.h"
#include "music.h"
#include "w_wad.h"

#define __FX_TRUE  (1 == 1)
#define __FX_FALSE (!__FX_TRUE)

#ifndef min
#define min(a, b)  (((a) < (b)) ? (a) : (b))
#endif

#ifndef max
#define max(a, b)  (((a) > (b)) ? (a) : (b))
#endif

typedef struct __DUKECHANINFO
{
    int in_use;
    int priority;
    unsigned long birthday;
    unsigned long callbackval;
    Mix_Chunk *chunk;
} duke_channel_info;

static char warningMessage[80];
static char errorMessage[80];
static int fx_initialized = 0;
static int numChannels = 0;
static void (*callback)(unsigned long) = NULL;
static int reverseStereo = 0;
static int reverbDelay = 256;
static int reverbLevel = 0;
static int fastReverb = 0;
static int maxReverbDelay = 256;
static int mixerIsStereo = 1;
static duke_channel_info *chaninfo = NULL;

static int music_initialized = 0;
static int music_context = 0;
static int music_loopflag = MUSIC_PlayOnce;
static unsigned char *music_songdata = NULL;
static int music_songlen = 0;
static Mix_Music *music_musicchunk = NULL;

int MUSIC_ErrorCode = MUSIC_Ok;

#define HandleOffset       0

#define MV_MaxPanPosition  31
#define MV_NumPanPositions ( MV_MaxPanPosition + 1 )
#define MV_MaxVolume       63

#define MIX_VOLUME( volume ) \
   ( ( max( 0, min( ( volume ), 255 ) ) * ( MV_MaxVolume + 1 ) ) >> 8 )

typedef struct
{
    unsigned char left;
    unsigned char right;
} Pan;

static Pan MV_PanTable[ MV_NumPanPositions ][ MV_MaxVolume + 1 ];

extern int SoundNumber(int x);

static void setWarningMessage(const char *msg)
{
    strncpy(warningMessage, msg, sizeof(warningMessage));
    warningMessage[sizeof(warningMessage) - 1] = '\0';
}

static void setErrorMessage(const char *msg)
{
    strncpy(errorMessage, msg, sizeof(errorMessage));
    errorMessage[sizeof(errorMessage) - 1] = '\0';
}

static void free_channel_chunk(int channel)
{
    if ((chaninfo != NULL) && (channel >= 0) && (channel < numChannels))
    {
        if (chaninfo[channel].chunk != NULL)
        {
            Mix_FreeChunk(chaninfo[channel].chunk);
            chaninfo[channel].chunk = NULL;
        }
    }
}

static void channelDoneCallback(int channel)
{
    unsigned long cb = (unsigned long) -1;

    if ((chaninfo == NULL) || (channel < 0) || (channel >= numChannels))
    {
        return;
    }

    cb = chaninfo[channel].callbackval;
    chaninfo[channel].in_use = 0;
    chaninfo[channel].priority = 0;
    chaninfo[channel].birthday = 0;
    chaninfo[channel].callbackval = (unsigned long) -1;
    free_channel_chunk(channel);

    if ((callback != NULL) && (cb != (unsigned long) -1))
    {
        callback(cb);
    }
}

static int ensure_audio(unsigned mixrate, int channels, int samplebits)
{
    Uint16 format = AUDIO_U8;

    if (!SDL_WasInit(SDL_INIT_AUDIO))
    {
        if (!SDL_WasInit(0))
        {
            if (SDL_Init(SDL_INIT_AUDIO) != 0)
            {
                setErrorMessage(SDL_GetError());
                return FX_Error;
            }
        }
        else if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
        {
            setErrorMessage(SDL_GetError());
            return FX_Error;
        }
    }

    if (Mix_QuerySpec(NULL, NULL, NULL))
    {
        return FX_Ok;
    }

    if (samplebits == 16)
    {
        format = AUDIO_S16SYS;
    }

    if (Mix_OpenAudio((int) mixrate, format, channels, 1024) < 0)
    {
        setErrorMessage(Mix_GetError());
        return FX_Error;
    }

    return FX_Ok;
}

static void MV_CalcPanTable(void)
{
    int level;
    int angle;
    int distance;
    int HalfAngle;
    int ramp;

    HalfAngle = (MV_NumPanPositions / 2);

    for (distance = 0; distance <= MV_MaxVolume; distance++)
    {
        level = (255 * (MV_MaxVolume - distance)) / MV_MaxVolume;
        for (angle = 0; angle <= HalfAngle / 2; angle++)
        {
            ramp = level - ((level * angle) / (MV_NumPanPositions / 4));

            MV_PanTable[angle][distance].left = (unsigned char) ramp;
            MV_PanTable[HalfAngle - angle][distance].left = (unsigned char) ramp;
            MV_PanTable[HalfAngle + angle][distance].left = (unsigned char) level;
            MV_PanTable[MV_MaxPanPosition - angle][distance].left = (unsigned char) level;

            MV_PanTable[angle][distance].right = (unsigned char) level;
            MV_PanTable[HalfAngle - angle][distance].right = (unsigned char) level;
            MV_PanTable[HalfAngle + angle][distance].right = (unsigned char) ramp;
            MV_PanTable[MV_MaxPanPosition - angle][distance].right = (unsigned char) ramp;
        }
    }
}

static int grabMixerChannel(int priority)
{
    int i;
    int replaceable = -1;

    for (i = 0; i < numChannels; i++)
    {
        if ((!chaninfo[i].in_use) || (!Mix_Playing(i)))
        {
            chaninfo[i].in_use = 1;
            chaninfo[i].priority = priority;
            chaninfo[i].birthday = (unsigned long) GetTicCount();
            return i;
        }

        if ((replaceable == -1) || (chaninfo[i].birthday < chaninfo[replaceable].birthday))
        {
            replaceable = i;
        }
    }

    if (replaceable != -1)
    {
        Mix_HaltChannel(replaceable);
        channelDoneCallback(replaceable);
        chaninfo[replaceable].in_use = 1;
        chaninfo[replaceable].priority = priority;
        chaninfo[replaceable].birthday = (unsigned long) GetTicCount();
    }

    return replaceable;
}

static int doSetPan(int handle, int vol, int left, int right, int checkIfPlaying)
{
    int retval = FX_Warning;

    if ((handle < 0) || (handle >= numChannels))
    {
        setWarningMessage("Invalid handle in FX_SetPan().");
    }
    else if (checkIfPlaying && !Mix_Playing(handle))
    {
        setWarningMessage("Voice is no longer playing in FX_SetPan().");
    }
    else
    {
        if (mixerIsStereo)
        {
            Mix_SetPanning(handle, (Uint8) left, (Uint8) right);
        }

        Mix_Volume(handle, vol >> 1);
        retval = FX_Ok;
    }

    return retval;
}

static int _FX_SetPosition(int chan, int angle, int distance)
{
    int left;
    int right;
    int mid;
    int volume;

    if (distance < 0)
    {
        distance = -distance;
        angle += MV_NumPanPositions / 2;
    }

    volume = MIX_VOLUME(distance);
    angle &= MV_MaxPanPosition;

    left = MV_PanTable[angle][volume].left;
    right = MV_PanTable[angle][volume].right;
    mid = max(0, 255 - distance);

    return doSetPan(chan, mid, left, right, 0);
}

static int setupVocPlayback(char *ptr, int size, int priority, unsigned long callbackval,
                            int *chan, Mix_Chunk **chunk)
{
    SDL_RWops *rw;

    *chunk = NULL;

    if ((ptr == NULL) || (size <= 0))
    {
        setErrorMessage("Invalid sound buffer.");
        return FX_Error;
    }

    *chan = grabMixerChannel(priority);
    if (*chan == -1)
    {
        setErrorMessage("No available channels.");
        return FX_Error;
    }

    rw = SDL_RWFromMem((void *) ptr, size);
    if (rw == NULL)
    {
        chaninfo[*chan].in_use = 0;
        setErrorMessage("SDL_RWFromMem failed.");
        return FX_Error;
    }

    *chunk = Mix_LoadWAV_RW(rw, 1);
    if (*chunk == NULL)
    {
        chaninfo[*chan].in_use = 0;
        setErrorMessage(Mix_GetError());
        return FX_Error;
    }

    chaninfo[*chan].callbackval = callbackval;
    chaninfo[*chan].chunk = *chunk;

    return FX_Ok;
}

char *FX_ErrorString(int ErrorNumber)
{
    switch (ErrorNumber)
    {
        case FX_Warning: return warningMessage;
        case FX_Error: return errorMessage;
        case FX_Ok: return "OK; no error.";
        case FX_ASSVersion: return "Incorrect sound library version.";
        case FX_BlasterError: return "SoundBlaster Error.";
        case FX_SoundCardError: return "General sound card error.";
        case FX_InvalidCard: return "Invalid sound card.";
        case FX_MultiVocError: return "Multiple VOC error.";
        case FX_DPMI_Error: return "DPMI error.";
        default: return "Unknown error.";
    }
}

int FX_SetupCard(int SoundCard, fx_device *device)
{
    if (device == NULL)
    {
        setErrorMessage("fx_device is NULL in FX_SetupCard().");
        return FX_Error;
    }

    if (SoundCard != SoundScape)
    {
        setErrorMessage("Card not found.");
        return FX_Error;
    }

    device->MaxVoices = 32;
    device->MaxSampleBits = 16;
    device->MaxChannels = 2;

    return FX_Ok;
}

int FX_Init(int SoundCard, int numvoices, int numchannels, int samplebits, unsigned mixrate)
{
    int status;

    if (fx_initialized)
    {
        setErrorMessage("Sound system is already initialized.");
        return FX_Error;
    }

    if (SoundCard != SoundScape)
    {
        setErrorMessage("Card not found.");
        return FX_Error;
    }

    if (numvoices <= 0)
    {
        numvoices = 16;
    }

    status = ensure_audio(mixrate, (numchannels == StereoFx) ? 2 : 1, samplebits);
    if (status != FX_Ok)
    {
        return status;
    }

    MV_CalcPanTable();

    Mix_AllocateChannels(numvoices);
    numChannels = Mix_AllocateChannels(-1);

    chaninfo = (duke_channel_info *) malloc(sizeof(duke_channel_info) * (size_t) numChannels);
    if (chaninfo == NULL)
    {
        setErrorMessage("Out of memory.");
        Mix_CloseAudio();
        return FX_Error;
    }

    memset(chaninfo, '\0', sizeof(duke_channel_info) * (size_t) numChannels);
    Mix_ChannelFinished(channelDoneCallback);

    mixerIsStereo = 1;
    maxReverbDelay = (int) mixrate;
    fx_initialized = 1;

    return FX_Ok;
}

int FX_Shutdown(void)
{
    int i;

    if (!fx_initialized)
    {
        setErrorMessage("Sound system is not currently initialized.");
        return FX_Error;
    }

    Mix_HaltChannel(-1);

    for (i = 0; i < numChannels; i++)
    {
        free_channel_chunk(i);
    }

    free(chaninfo);
    chaninfo = NULL;

    Mix_CloseAudio();

    reverseStereo = 0;
    reverbLevel = 0;
    fastReverb = 0;
    maxReverbDelay = 256;
    fx_initialized = 0;
    numChannels = 0;

    return FX_Ok;
}

int FX_SetCallBack(void (*func)(unsigned long))
{
    callback = func;
    return FX_Ok;
}

void FX_SetVolume(int volume)
{
    Mix_Volume(-1, volume >> 1);
}

int FX_GetVolume(void)
{
    return Mix_Volume(-1, -1) << 1;
}

void FX_SetReverseStereo(int setting)
{
    reverseStereo = (setting != 0);
}

int FX_GetReverseStereo(void)
{
    return reverseStereo;
}

void FX_SetReverb(int reverb)
{
    reverbLevel = reverb;
    fastReverb = 0;
}

void FX_SetFastReverb(int reverb)
{
    reverbLevel = reverb;
    fastReverb = 1;
}

int FX_GetMaxReverbDelay(void)
{
    return maxReverbDelay;
}

int FX_GetReverbDelay(void)
{
    return reverbDelay;
}

void FX_SetReverbDelay(int delay)
{
    if (delay < 256) delay = 256;
    if (delay > maxReverbDelay) delay = maxReverbDelay;
    reverbDelay = delay;
}

int FX_VoiceAvailable(int priority)
{
    int chan = grabMixerChannel(priority);
    int rc = (chan != -1);

    if (rc)
    {
        chaninfo[chan].in_use = 0;
    }

    return rc;
}

int FX_EndLooping(int handle)
{
    (void) handle;
    return FX_Ok;
}

int FX_SetPan(int handle, int vol, int left, int right)
{
    return doSetPan(handle - HandleOffset, vol, left, right, 1);
}

int FX_SetPitch(int handle, int pitchoffset)
{
    (void) handle;
    (void) pitchoffset;
    return FX_Ok;
}

int FX_SetFrequency(int handle, int frequency)
{
    (void) handle;
    (void) frequency;
    return FX_Ok;
}

int FX_PlayVOC(char *ptr, int pitchoffset, int vol, int left, int right,
               int priority, unsigned long callbackval)
{
    int rc;
    int chan;
    int len;
    Mix_Chunk *chunk;

    (void) pitchoffset;

    len = W_LumpLength(SoundNumber((int) callbackval));
    rc = setupVocPlayback(ptr, len, priority, callbackval, &chan, &chunk);
    if (rc != FX_Ok)
    {
        return rc;
    }

    rc = doSetPan(chan, vol, left, right, 0);
    if (rc != FX_Ok)
    {
        channelDoneCallback(chan);
        return rc;
    }

    if (Mix_PlayChannel(chan, chunk, 0) == -1)
    {
        channelDoneCallback(chan);
        setErrorMessage(Mix_GetError());
        return FX_Error;
    }

    return HandleOffset + chan;
}

int FX_PlayLoopedVOC(char *ptr, long loopstart, long loopend,
                     int pitchoffset, int vol, int left, int right, int priority,
                     unsigned long callbackval)
{
    int rc;
    int chan;
    int len;
    Mix_Chunk *chunk;

    (void) loopstart;
    (void) loopend;
    (void) pitchoffset;

    len = W_LumpLength(SoundNumber((int) callbackval));
    rc = setupVocPlayback(ptr, len, priority, callbackval, &chan, &chunk);
    if (rc != FX_Ok)
    {
        return rc;
    }

    rc = doSetPan(chan, vol, left, right, 0);
    if (rc != FX_Ok)
    {
        channelDoneCallback(chan);
        return rc;
    }

    if (Mix_PlayChannel(chan, chunk, -1) == -1)
    {
        channelDoneCallback(chan);
        setErrorMessage(Mix_GetError());
        return FX_Error;
    }

    return HandleOffset + chan;
}

int FX_PlayVOC3D(char *ptr, int pitchoffset, int angle, int distance,
                 int priority, unsigned long callbackval)
{
    int rc;
    int chan;
    int len;
    Mix_Chunk *chunk;

    (void) pitchoffset;

    len = W_LumpLength(SoundNumber((int) callbackval));
    rc = setupVocPlayback(ptr, len, priority, callbackval, &chan, &chunk);
    if (rc != FX_Ok)
    {
        return rc;
    }

    _FX_SetPosition(chan, angle, distance);

    if (Mix_PlayChannel(chan, chunk, 0) == -1)
    {
        channelDoneCallback(chan);
        setErrorMessage(Mix_GetError());
        return FX_Error;
    }

    return HandleOffset + chan;
}

int FX_PlayVOC3D_ROTT(char *ptr, int size, int pitchoffset, int angle, int distance,
                      int priority, unsigned long callbackval)
{
    int rc;
    int chan;
    Mix_Chunk *chunk;

    (void) pitchoffset;

    rc = setupVocPlayback(ptr, size, priority, callbackval, &chan, &chunk);
    if (rc != FX_Ok)
    {
        return rc;
    }

    _FX_SetPosition(chan, angle, distance);

    if (Mix_PlayChannel(chan, chunk, 0) == -1)
    {
        channelDoneCallback(chan);
        setErrorMessage(Mix_GetError());
        return FX_Error;
    }

    return HandleOffset + chan;
}

int FX_PlayWAV(char *ptr, int pitchoffset, int vol, int left, int right,
               int priority, unsigned long callbackval)
{
    return FX_PlayVOC(ptr, pitchoffset, vol, left, right, priority, callbackval);
}

int FX_PlayLoopedWAV(char *ptr, long loopstart, long loopend,
                     int pitchoffset, int vol, int left, int right, int priority,
                     unsigned long callbackval)
{
    return FX_PlayLoopedVOC(ptr, loopstart, loopend, pitchoffset, vol, left, right,
                            priority, callbackval);
}

int FX_PlayWAV3D(char *ptr, int pitchoffset, int angle, int distance,
                 int priority, unsigned long callbackval)
{
    return FX_PlayVOC3D(ptr, pitchoffset, angle, distance, priority, callbackval);
}

int FX_PlayWAV3D_ROTT(char *ptr, int size, int pitchoffset, int angle, int distance,
                      int priority, unsigned long callbackval)
{
    return FX_PlayVOC3D_ROTT(ptr, size, pitchoffset, angle, distance, priority, callbackval);
}

int FX_PlayRaw(char *ptr, unsigned long length, unsigned rate,
               int pitchoffset, int vol, int left, int right, int priority,
               unsigned long callbackval)
{
    (void) ptr;
    (void) length;
    (void) rate;
    (void) pitchoffset;
    (void) vol;
    (void) left;
    (void) right;
    (void) priority;
    (void) callbackval;
    setErrorMessage("FX_PlayRaw() not implemented.");
    return FX_Error;
}

int FX_PlayLoopedRaw(char *ptr, unsigned long length, char *loopstart,
                     char *loopend, unsigned rate, int pitchoffset, int vol, int left,
                     int right, int priority, unsigned long callbackval)
{
    (void) ptr;
    (void) length;
    (void) loopstart;
    (void) loopend;
    (void) rate;
    (void) pitchoffset;
    (void) vol;
    (void) left;
    (void) right;
    (void) priority;
    (void) callbackval;
    setErrorMessage("FX_PlayLoopedRaw() not implemented.");
    return FX_Error;
}

int FX_Pan3D(int handle, int angle, int distance)
{
    int chan = handle - HandleOffset;

    if ((chan < 0) || (chan >= numChannels))
    {
        setWarningMessage("Invalid handle in FX_Pan3D().");
        return FX_Warning;
    }

    if (!Mix_Playing(chan))
    {
        setWarningMessage("Voice is no longer playing in FX_Pan3D().");
        return FX_Warning;
    }

    _FX_SetPosition(chan, angle, distance);
    return FX_Ok;
}

int FX_SoundActive(int handle)
{
    handle -= HandleOffset;

    if ((handle < 0) || (handle >= numChannels))
    {
        return __FX_FALSE;
    }

    return Mix_Playing(handle) ? __FX_TRUE : __FX_FALSE;
}

int FX_SoundsPlaying(void)
{
    return Mix_Playing(-1);
}

int FX_StopSound(int handle)
{
    handle -= HandleOffset;

    if ((handle < 0) || (handle >= numChannels))
    {
        setWarningMessage("Invalid handle in FX_StopSound().");
        return FX_Warning;
    }

    Mix_HaltChannel(handle);
    channelDoneCallback(handle);

    return FX_Ok;
}

int FX_StopAllSounds(void)
{
    int i;

    Mix_HaltChannel(-1);

    for (i = 0; i < numChannels; i++)
    {
        channelDoneCallback(i);
    }

    return FX_Ok;
}

int FX_StartDemandFeedPlayback(void (*function)(char **ptr, unsigned long *length),
                               int rate, int pitchoffset, int vol, int left, int right,
                               int priority, unsigned long callbackval)
{
    (void) function;
    (void) rate;
    (void) pitchoffset;
    (void) vol;
    (void) left;
    (void) right;
    (void) priority;
    (void) callbackval;
    setErrorMessage("FX_StartDemandFeedPlayback() not implemented.");
    return FX_Error;
}

int FX_StartRecording(int MixRate, void (*function)(char *ptr, int length))
{
    (void) MixRate;
    (void) function;
    setErrorMessage("FX_StartRecording() not implemented.");
    return FX_Error;
}

void FX_StopRecord(void)
{
}

char *MUSIC_ErrorString(int ErrorNumber)
{
    switch (ErrorNumber)
    {
        case MUSIC_Warning: return warningMessage;
        case MUSIC_Error: return errorMessage;
        case MUSIC_Ok: return "OK; no error.";
        case MUSIC_ASSVersion: return "Incorrect sound library version.";
        case MUSIC_SoundCardError: return "General sound card error.";
        case MUSIC_InvalidCard: return "Invalid sound card.";
        case MUSIC_MidiError: return "MIDI error.";
        case MUSIC_MPU401Error: return "MPU401 error.";
        case MUSIC_TaskManError: return "Task Manager error.";
        case MUSIC_FMNotDetected: return "FM not detected error.";
        case MUSIC_DPMI_Error: return "DPMI error.";
        default: return "Unknown error.";
    }
}

int MUSIC_Init(int SoundCard, int Address)
{
    (void) Address;

    if (music_initialized)
    {
        setErrorMessage("Music system is already initialized.");
        return MUSIC_Error;
    }

    if (SoundCard != SoundScape)
    {
        setErrorMessage("Card not found.");
        return MUSIC_Error;
    }

    if (ensure_audio(11025, 2, 16) != FX_Ok)
    {
        return MUSIC_Error;
    }

    music_initialized = 1;
    return MUSIC_Ok;
}

int MUSIC_Shutdown(void)
{
    if (!music_initialized)
    {
        setErrorMessage("Music system is not currently initialized.");
        return MUSIC_Error;
    }

    MUSIC_StopSong();
    music_context = 0;
    music_initialized = 0;
    music_loopflag = MUSIC_PlayOnce;

    return MUSIC_Ok;
}

void MUSIC_SetMaxFMMidiChannel(int channel)
{
    (void) channel;
}

void MUSIC_SetVolume(int volume)
{
    Mix_VolumeMusic(volume >> 1);
}

void MUSIC_SetMidiChannelVolume(int channel, int volume)
{
    (void) channel;
    (void) volume;
}

void MUSIC_ResetMidiChannelVolumes(void)
{
}

int MUSIC_GetVolume(void)
{
    return Mix_VolumeMusic(-1) << 1;
}

void MUSIC_SetLoopFlag(int loopflag)
{
    music_loopflag = loopflag;
}

int MUSIC_SongPlaying(void)
{
    return (Mix_PlayingMusic() || Mix_PausedMusic()) ? __FX_TRUE : __FX_FALSE;
}

void MUSIC_Continue(void)
{
    if (Mix_PausedMusic())
    {
        Mix_ResumeMusic();
    }
    else if (!Mix_PlayingMusic() && (music_songdata != NULL))
    {
        if (music_songlen > 0)
        {
            MUSIC_PlaySongROTT(music_songdata, music_songlen, music_loopflag);
        }
        else
        {
            MUSIC_PlaySong(music_songdata, music_loopflag);
        }
    }
}

void MUSIC_Pause(void)
{
    Mix_PauseMusic();
}

int MUSIC_StopSong(void)
{
    if (Mix_PlayingMusic() || Mix_PausedMusic())
    {
        Mix_HaltMusic();
    }

    if (music_musicchunk != NULL)
    {
        Mix_FreeMusic(music_musicchunk);
        music_musicchunk = NULL;
    }

    music_songdata = NULL;
    music_songlen = 0;

    return MUSIC_Ok;
}

int MUSIC_PlaySong(unsigned char *song, int loopflag)
{
    SDL_RWops *rw;

    MUSIC_StopSong();

    music_songdata = song;

    rw = SDL_RWFromMem((void *) song, (10 * 1024) * 1024);
    if (rw == NULL)
    {
        setErrorMessage("SDL_RWFromMem failed while loading music.");
        return MUSIC_Error;
    }

    music_musicchunk = Mix_LoadMUS_RW(rw, 1);
    if (music_musicchunk == NULL)
    {
        setErrorMessage(Mix_GetError());
        return MUSIC_Error;
    }

    if (Mix_PlayMusic(music_musicchunk, (loopflag == MUSIC_PlayOnce) ? 0 : -1) == -1)
    {
        setErrorMessage(Mix_GetError());
        return MUSIC_Error;
    }

    return MUSIC_Ok;
}

int MUSIC_PlaySongROTT(unsigned char *song, int size, int loopflag)
{
    SDL_RWops *rw;
    FILE *fp;
    size_t wrote;
    const char *tmpmid = "/rott-song.mid";

    MUSIC_StopSong();

    music_songdata = song;
    music_songlen = size;

    rw = SDL_RWFromMem((void *) song, size);
    if (rw == NULL)
    {
        setErrorMessage("SDL_RWFromMem failed while loading music.");
        return MUSIC_Error;
    }

    music_musicchunk = Mix_LoadMUS_RW(rw, 1);
    if (music_musicchunk == NULL)
    {
        /* Some SDL_mixer MIDI backends are more reliable with a filename. */
        fp = fopen(tmpmid, "wb");
        if (fp != NULL)
        {
            wrote = fwrite(song, 1, (size_t) size, fp);
            fclose(fp);
            if (wrote == (size_t) size)
            {
                music_musicchunk = Mix_LoadMUS(tmpmid);
            }
        }

        if (music_musicchunk == NULL)
        {
            setErrorMessage(Mix_GetError());
            return MUSIC_Error;
        }
    }

    if (Mix_PlayMusic(music_musicchunk, (loopflag == MUSIC_PlayOnce) ? 0 : -1) == -1)
    {
        setErrorMessage(Mix_GetError());
        return MUSIC_Error;
    }

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
    (void) PositionInTicks;
}

void MUSIC_SetSongTime(unsigned long milliseconds)
{
    (void) milliseconds;
}

void MUSIC_SetSongPosition(int measure, int beat, int tick)
{
    (void) measure;
    (void) beat;
    (void) tick;
}

void MUSIC_GetSongPosition(songposition *pos)
{
    if (pos != NULL)
    {
        memset(pos, 0, sizeof(*pos));
    }
}

void MUSIC_GetSongLength(songposition *pos)
{
    if (pos != NULL)
    {
        memset(pos, 0, sizeof(*pos));
    }
}

int MUSIC_FadeVolume(int tovolume, int milliseconds)
{
    (void) tovolume;
    Mix_FadeOutMusic(milliseconds);
    return MUSIC_Ok;
}

int MUSIC_FadeActive(void)
{
    return (Mix_FadingMusic() == MIX_FADING_OUT) ? __FX_TRUE : __FX_FALSE;
}

void MUSIC_StopFade(void)
{
}

void MUSIC_RerouteMidiChannel(int channel, int cdecl (*function)(int event, int c1, int c2))
{
    (void) channel;
    (void) function;
}

void MUSIC_RegisterTimbreBank(unsigned char *timbres)
{
    (void) timbres;
}
