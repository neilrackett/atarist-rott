/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <math.h>

#include "SDL.h"

#include "rt_def.h"
#include "w_wad.h"
#include "fx_man.h"
#include "music.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif

#if defined(__MINT__)
#define FX_MAX_VOICES 8
#define FX_PLATFORM_MAX_VOICES 4
#define FX_DEFAULT_RATE 11025
#define FX_BUFFER_SAMPLES 1024
#define FX_MIX_ATTENUATION_SHIFT 1
#define MIDI_MAX_VOICES 8
#define FX_ENABLE_MUSIC_SYNTH 1
#else
#define FX_MAX_VOICES 16
#define FX_PLATFORM_MAX_VOICES 8
#define FX_DEFAULT_RATE 11025
#define FX_BUFFER_SAMPLES 512
#define FX_MIX_ATTENUATION_SHIFT 0
#define MIDI_MAX_VOICES 24
#define FX_ENABLE_MUSIC_SYNTH 1
#endif

#define FX_CALLBACK_QUEUE 256
#define FX_FREE_QUEUE 128
#define FX_HANDLE_START 1

#define MUSIC_TICK_FP_SHIFT 16
#define FX_MAX_SAMPLE_FRAMES 65536

#define MV_MaxPanPosition 31
#define MV_NumPanPositions (MV_MaxPanPosition + 1)
#define MV_MaxVolume 63
#define MIX_VOLUME(volume) ((max(0, min((volume), 255)) * (MV_MaxVolume + 1)) >> 8)

#define MIDI_DEFAULT_DIVISION 96
#define MIDI_DEFAULT_TEMPO_US 500000UL

typedef struct
{
    unsigned char left;
    unsigned char right;
} pan_t;

typedef struct
{
    int active;
    int handle;
    int priority;
    unsigned long birthday;
    unsigned long callbackval;
    Sint16 *samples;
    int frames;
    int pos_fp;
    int base_step_fp;
    int step_fp;
    int vol;
    int left;
    int right;
} fx_voice_t;

typedef struct
{
    Sint16 *samples;
    int frames;
    int rate;
} decoded_sample_t;

typedef enum
{
    MIDI_EVENT_NOTE_OFF = 0,
    MIDI_EVENT_NOTE_ON = 1,
    MIDI_EVENT_TEMPO = 2
} midi_event_type_t;

typedef struct
{
    unsigned long tick;
    unsigned long order;
    unsigned char type;
    unsigned char channel;
    unsigned char a;
    unsigned char b;
    unsigned long tempo_us;
} midi_event_t;

typedef struct
{
    midi_event_t *items;
    int count;
    int cap;
    unsigned long order_counter;
} midi_builder_t;

typedef struct
{
    int active;
    unsigned char channel;
    unsigned char note;
    int level;
    unsigned int phase;
    unsigned int step;
    unsigned long age;
} midi_voice_t;

int FX_SoundDevice = -1;
int FX_ErrorCode = FX_Ok;
int FX_Installed = 0;
int MUSIC_ErrorCode = MUSIC_Ok;

static char fx_error_message[96];
static char fx_warning_message[96];
static char music_error_message[96];
static char music_warning_message[96];

static pan_t MV_PanTable[MV_NumPanPositions][MV_MaxVolume + 1];

static fx_voice_t fx_voices[FX_MAX_VOICES];
static int fx_num_voices = 8;
static int fx_mix_rate = FX_DEFAULT_RATE;
static int fx_global_volume = 255;
static int fx_reverse_stereo = 0;
static int fx_reverb = 0;
static int fx_fast_reverb = 0;
static int fx_reverb_delay = 256;
static int fx_audio_opened = 0;
static int fx_audio_owned = 0;
static int fx_next_handle = FX_HANDLE_START;
static void (*fx_callback)(unsigned long) = NULL;

static unsigned long fx_cb_queue[FX_CALLBACK_QUEUE];
static int fx_cb_read = 0;
static int fx_cb_write = 0;
static Sint16 *fx_free_queue[FX_FREE_QUEUE];
static int fx_free_read = 0;
static int fx_free_write = 0;
static int fx_in_audio_callback = 0;

static int music_initialized = 0;
static int music_owns_fx = 0;
static int music_volume = 255;
static int music_loopflag = MUSIC_PlayOnce;
static int music_playing = 0;
static int music_paused = 0;
static int music_fade_active = 0;
static int music_context = 0;
static unsigned long music_samples_played = 0;
static unsigned long music_tempo_us = MIDI_DEFAULT_TEMPO_US;
static unsigned short music_division = MIDI_DEFAULT_DIVISION;
static unsigned long music_tick_fp = 0;
static unsigned long music_ticks_per_sample_fp = 1;
static unsigned long music_voice_age = 1;
static unsigned int music_note_step[128];

static midi_event_t *music_events = NULL;
static int music_event_count = 0;
static int music_event_index = 0;
static midi_voice_t music_voices[MIDI_MAX_VOICES];

extern int SoundNumber(int x);

static void fx_set_error(const char *msg)
{
    strncpy(fx_error_message, msg, sizeof(fx_error_message));
    fx_error_message[sizeof(fx_error_message) - 1] = '\0';
}

static void fx_set_warning(const char *msg)
{
    strncpy(fx_warning_message, msg, sizeof(fx_warning_message));
    fx_warning_message[sizeof(fx_warning_message) - 1] = '\0';
}

static void music_set_error(const char *msg)
{
    strncpy(music_error_message, msg, sizeof(music_error_message));
    music_error_message[sizeof(music_error_message) - 1] = '\0';
}

static void music_set_warning(const char *msg)
{
    strncpy(music_warning_message, msg, sizeof(music_warning_message));
    music_warning_message[sizeof(music_warning_message) - 1] = '\0';
}

static void MV_CalcPanTable(void)
{
    int level;
    int angle;
    int distance;
    int half_angle;
    int ramp;

    half_angle = (MV_NumPanPositions / 2);

    for (distance = 0; distance <= MV_MaxVolume; distance++)
    {
        level = (255 * (MV_MaxVolume - distance)) / MV_MaxVolume;
        for (angle = 0; angle <= half_angle / 2; angle++)
        {
            ramp = level - ((level * angle) / (MV_NumPanPositions / 4));

            MV_PanTable[angle][distance].left = (unsigned char)ramp;
            MV_PanTable[half_angle - angle][distance].left = (unsigned char)ramp;
            MV_PanTable[half_angle + angle][distance].left = (unsigned char)level;
            MV_PanTable[MV_MaxPanPosition - angle][distance].left = (unsigned char)level;

            MV_PanTable[angle][distance].right = (unsigned char)level;
            MV_PanTable[half_angle - angle][distance].right = (unsigned char)level;
            MV_PanTable[half_angle + angle][distance].right = (unsigned char)ramp;
            MV_PanTable[MV_MaxPanPosition - angle][distance].right = (unsigned char)ramp;
        }
    }
}

static int fx_scale_step_by_cents(int base_step_fp, int pitchoffset)
{
    long step;

    step = base_step_fp;
    if (step <= 0)
        step = 256;

    while (pitchoffset >= 1200)
    {
        step <<= 1;
        pitchoffset -= 1200;
        if (step > INT_MAX)
        {
            step = INT_MAX;
            break;
        }
    }

    while (pitchoffset <= -1200)
    {
        step >>= 1;
        pitchoffset += 1200;
        if (step < 256)
        {
            step = 256;
            break;
        }
    }

    step += (step * pitchoffset) / 1200;
    if (step < 256)
        step = 256;
    if (step > INT_MAX)
        step = INT_MAX;

    return (int)step;
}

static void fx_queue_sample_free_locked(Sint16 *samples)
{
    int next;

    if (samples == NULL)
        return;

    next = (fx_free_write + 1) % FX_FREE_QUEUE;
    if (next == fx_free_read)
    {
        free(samples);
        return;
    }

    fx_free_queue[fx_free_write] = samples;
    fx_free_write = next;
}

static void fx_flush_sample_free_queue(void)
{
    Sint16 *local[FX_FREE_QUEUE];
    int count;
    int i;

    count = 0;

    if (fx_audio_opened)
    {
        SDL_LockAudio();
        while (fx_free_read != fx_free_write && count < FX_FREE_QUEUE)
        {
            local[count++] = fx_free_queue[fx_free_read];
            fx_free_queue[fx_free_read] = NULL;
            fx_free_read = (fx_free_read + 1) % FX_FREE_QUEUE;
        }
        SDL_UnlockAudio();
    }
    else
    {
        while (fx_free_read != fx_free_write && count < FX_FREE_QUEUE)
        {
            local[count++] = fx_free_queue[fx_free_read];
            fx_free_queue[fx_free_read] = NULL;
            fx_free_read = (fx_free_read + 1) % FX_FREE_QUEUE;
        }
    }

    for (i = 0; i < count; i++)
    {
        free(local[i]);
    }
}

static void music_build_note_steps_locked(void)
{
    int i;

    for (i = 0; i < 128; i++)
    {
        double hz;
        double step;

        hz = 440.0 * pow(2.0, ((double)(i - 69)) / 12.0);
        step = (hz * 65536.0) / (double)fx_mix_rate;
        if (step < 1.0)
            step = 1.0;
        music_note_step[i] = (unsigned int)step;
    }
}

static unsigned short fx_read_le16(const unsigned char *p)
{
    return (unsigned short)(p[0] | (p[1] << 8));
}

static unsigned long fx_read_le24(const unsigned char *p)
{
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16);
}

static unsigned long fx_read_le32(const unsigned char *p)
{
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8) |
           ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

static unsigned short fx_read_be16(const unsigned char *p)
{
    return (unsigned short)((p[0] << 8) | p[1]);
}

static unsigned long fx_read_be24(const unsigned char *p)
{
    return ((unsigned long)p[0] << 16) | ((unsigned long)p[1] << 8) | (unsigned long)p[2];
}

static unsigned long fx_read_be32(const unsigned char *p)
{
    return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) |
           ((unsigned long)p[2] << 8) | (unsigned long)p[3];
}

static int fx_reserve_samples(Sint16 **samples, int *cap, int needed)
{
    Sint16 *tmp;
    int new_cap;

    if (needed <= *cap)
        return 1;

    new_cap = (*cap > 0) ? *cap : 2048;
    while (new_cap < needed)
    {
        if (new_cap > (INT_MAX / 2))
            return 0;
        new_cap *= 2;
    }

    tmp = (Sint16 *)realloc(*samples, (size_t)new_cap * sizeof(Sint16));
    if (tmp == NULL)
        return 0;

    *samples = tmp;
    *cap = new_cap;
    return 1;
}

static int fx_append_raw_block(const unsigned char *src, int bytes, int bits, int channels,
                               Sint16 **samples, int *frames, int *cap)
{
    int bytes_per_sample;
    int count;
    int i;
    int dst_index;
    int sample;
    int sample_l;
    int sample_r;

    if ((bits != 8 && bits != 16) || (channels != 1 && channels != 2))
        return 0;

    bytes_per_sample = (bits / 8) * channels;
    if (bytes_per_sample <= 0)
        return 0;

    count = bytes / bytes_per_sample;
    if (count <= 0)
        return 1;

    if (*frames >= FX_MAX_SAMPLE_FRAMES)
        return 1;
    if (*frames + count > FX_MAX_SAMPLE_FRAMES)
        count = FX_MAX_SAMPLE_FRAMES - *frames;
    if (count <= 0)
        return 1;

    if (!fx_reserve_samples(samples, cap, *frames + count))
        return 0;

    dst_index = *frames;
    for (i = 0; i < count; i++)
    {
        if (bits == 8)
        {
            if (channels == 1)
            {
                sample = ((int)src[0] - 128) << 8;
                src += 1;
            }
            else
            {
                sample_l = ((int)src[0] - 128) << 8;
                sample_r = ((int)src[1] - 128) << 8;
                sample = (sample_l + sample_r) / 2;
                src += 2;
            }
        }
        else
        {
            if (channels == 1)
            {
                sample = (Sint16)fx_read_le16(src);
                src += 2;
            }
            else
            {
                sample_l = (Sint16)fx_read_le16(src);
                sample_r = (Sint16)fx_read_le16(src + 2);
                sample = (sample_l + sample_r) / 2;
                src += 4;
            }
        }

        (*samples)[dst_index++] = (Sint16)sample;
    }

    *frames += count;
    return 1;
}

static int fx_append_silence(int count, Sint16 **samples, int *frames, int *cap)
{
    int dst;

    if (count <= 0)
        return 1;

    if (*frames >= FX_MAX_SAMPLE_FRAMES)
        return 1;
    if (*frames + count > FX_MAX_SAMPLE_FRAMES)
        count = FX_MAX_SAMPLE_FRAMES - *frames;
    if (count <= 0)
        return 1;

    if (!fx_reserve_samples(samples, cap, *frames + count))
        return 0;

    dst = *frames;
    memset((*samples) + dst, 0, (size_t)count * sizeof(Sint16));
    *frames += count;
    return 1;
}

static int fx_decode_wav(const unsigned char *ptr, int size, decoded_sample_t *out)
{
    int pos;
    int cap;
    int fmt_found;
    int data_found;
    int channels;
    int bits;
    int sample_rate;
    const unsigned char *data_ptr;
    int data_size;
    unsigned long chunk_size_u;
    int chunk_size;

    pos = 12;
    fmt_found = 0;
    data_found = 0;
    channels = 0;
    bits = 0;
    sample_rate = FX_DEFAULT_RATE;
    data_ptr = NULL;
    data_size = 0;

    if (size < 44)
        return 0;
    if (memcmp(ptr, "RIFF", 4) != 0)
        return 0;
    if (memcmp(ptr + 8, "WAVE", 4) != 0)
        return 0;

    while (pos + 8 <= size)
    {
        chunk_size_u = fx_read_le32(ptr + pos + 4);
        if (chunk_size_u > (unsigned long)INT_MAX)
            return 0;
        chunk_size = (int)chunk_size_u;

        if (memcmp(ptr + pos, "fmt ", 4) == 0)
        {
            if (chunk_size < 16 || pos + 8 + chunk_size > size)
                return 0;
            if (fx_read_le16(ptr + pos + 8) != 1)
                return 0;

            channels = (int)fx_read_le16(ptr + pos + 10);
            sample_rate = (int)fx_read_le32(ptr + pos + 12);
            bits = (int)fx_read_le16(ptr + pos + 22);
            fmt_found = 1;
        }
        else if (memcmp(ptr + pos, "data", 4) == 0)
        {
            if (pos + 8 + chunk_size > size)
                return 0;
            data_ptr = ptr + pos + 8;
            data_size = chunk_size;
            data_found = 1;
        }

        pos += 8 + chunk_size;
        if (chunk_size & 1)
            pos++;
    }

    if (!fmt_found || !data_found)
        return 0;

    out->samples = NULL;
    out->frames = 0;
    out->rate = sample_rate > 0 ? sample_rate : FX_DEFAULT_RATE;
    cap = 0;

    return fx_append_raw_block(data_ptr, data_size, bits, channels,
                               &out->samples, &out->frames, &cap);
}

static int fx_decode_voc(const unsigned char *ptr, int size, decoded_sample_t *out)
{
    int pos;
    int cap;
    int bits;
    int channels;
    int rate;
    int has_extended;
    unsigned long block_size_u;
    int block_size;
    int payload;
    int rate_code;
    int pack;
    int mode;
    int period;
    unsigned short tc;
    unsigned long rate32;

    if (size < 32)
        return 0;
    if (memcmp(ptr, "Creative Voice File\032", 20) != 0)
        return 0;

    pos = (int)fx_read_le16(ptr + 20);
    if (pos < 0 || pos >= size)
        return 0;

    out->samples = NULL;
    out->frames = 0;
    out->rate = FX_DEFAULT_RATE;
    cap = 0;

    bits = 8;
    channels = 1;
    rate = FX_DEFAULT_RATE;
    has_extended = 0;

    while (pos + 4 <= size)
    {
        unsigned char block_type;

        block_type = ptr[pos++];
        if (block_type == 0)
            break;

        block_size_u = fx_read_le24(ptr + pos);
        pos += 3;
        if (block_size_u > (unsigned long)INT_MAX)
            return 0;
        block_size = (int)block_size_u;

        if (block_size < 0 || pos + block_size > size)
            return 0;

        if (block_type == 1)
        {
            if (block_size < 2)
                return 0;

            rate_code = ptr[pos];
            pack = ptr[pos + 1];
            pos += 2;
            payload = block_size - 2;

            if (!has_extended)
            {
                if (rate_code == 0)
                    return 0;
                rate = (int)(1000000L / (256 - rate_code));
                bits = 8;
                channels = 1;
            }
            has_extended = 0;

            if (pack != 0)
                return 0;

            if (!fx_append_raw_block(ptr + pos, payload, bits, channels,
                                     &out->samples, &out->frames, &cap))
                return 0;
            pos += payload;
        }
        else if (block_type == 2)
        {
            payload = block_size;
            if (!fx_append_raw_block(ptr + pos, payload, bits, channels,
                                     &out->samples, &out->frames, &cap))
                return 0;
            pos += payload;
        }
        else if (block_type == 3)
        {
            if (block_size < 3)
                return 0;
            period = (int)fx_read_le16(ptr + pos);
            if (period < 0)
                period = 0;
            if (!fx_append_silence(period, &out->samples, &out->frames, &cap))
                return 0;
            pos += block_size;
            has_extended = 0;
        }
        else if (block_type == 8)
        {
            if (block_size < 4)
                return 0;
            tc = fx_read_le16(ptr + pos);
            pack = ptr[pos + 2];
            mode = ptr[pos + 3];

            if (pack != 0)
                return 0;

            channels = mode ? 2 : 1;
            bits = 8;
            if (tc == 65535)
                return 0;
            rate = (int)((256000000UL / (65536UL - (unsigned long)tc)) / (unsigned long)channels);
            has_extended = 1;
            pos += block_size;
        }
        else if (block_type == 9)
        {
            if (block_size < 12)
                return 0;
            rate32 = fx_read_le32(ptr + pos);
            bits = ptr[pos + 4];
            channels = ptr[pos + 5];
            payload = block_size - 12;
            if (rate32 == 0 || (channels != 1 && channels != 2))
                return 0;
            rate = (int)rate32;

            if (!fx_append_raw_block(ptr + pos + 12, payload, bits, channels,
                                     &out->samples, &out->frames, &cap))
                return 0;
            pos += block_size;
            has_extended = 0;
        }
        else
        {
            pos += block_size;
            has_extended = 0;
        }
    }

    if (out->frames <= 0 || out->samples == NULL)
        return 0;

    out->rate = rate > 0 ? rate : FX_DEFAULT_RATE;
    return 1;
}

static int fx_decode_sample_data(const unsigned char *ptr, int size, decoded_sample_t *out)
{
    if (ptr == NULL || size <= 0)
        return 0;

    memset(out, 0, sizeof(*out));

    if (fx_decode_voc(ptr, size, out))
        return 1;
    if (fx_decode_wav(ptr, size, out))
        return 1;

    return 0;
}

static void fx_queue_callback_locked(unsigned long callbackval)
{
    int next;

    next = (fx_cb_write + 1) % FX_CALLBACK_QUEUE;
    if (next == fx_cb_read)
        return;

    fx_cb_queue[fx_cb_write] = callbackval;
    fx_cb_write = next;
}

static void music_clear_voices_locked(void)
{
    memset(music_voices, 0, sizeof(music_voices));
}

static void music_update_timing_locked(void)
{
    unsigned long long num;
    unsigned long long den;

    if (fx_mix_rate <= 0)
        fx_mix_rate = FX_DEFAULT_RATE;

    if (music_tempo_us == 0)
        music_tempo_us = MIDI_DEFAULT_TEMPO_US;

    num = ((unsigned long long)1000000ULL * (unsigned long long)music_division) << MUSIC_TICK_FP_SHIFT;
    den = (unsigned long long)music_tempo_us * (unsigned long long)fx_mix_rate;
    if (den == 0)
        den = 1;

    music_ticks_per_sample_fp = (unsigned long)(num / den);
    if (music_ticks_per_sample_fp == 0)
        music_ticks_per_sample_fp = 1;
}

static void music_apply_event_locked(const midi_event_t *ev)
{
    int i;
    int use_idx;

    if (ev->type == MIDI_EVENT_TEMPO)
    {
        if (ev->tempo_us > 0)
        {
            music_tempo_us = ev->tempo_us;
            music_update_timing_locked();
        }
        return;
    }

    if (ev->type == MIDI_EVENT_NOTE_OFF)
    {
        for (i = 0; i < MIDI_MAX_VOICES; i++)
        {
            if (music_voices[i].active &&
                music_voices[i].channel == ev->channel &&
                music_voices[i].note == ev->a)
            {
                music_voices[i].active = 0;
            }
        }
        return;
    }

    if (ev->type != MIDI_EVENT_NOTE_ON)
        return;

    for (i = 0; i < MIDI_MAX_VOICES; i++)
    {
        if (music_voices[i].active &&
            music_voices[i].channel == ev->channel &&
            music_voices[i].note == ev->a)
        {
            music_voices[i].active = 0;
        }
    }

    use_idx = -1;
    for (i = 0; i < MIDI_MAX_VOICES; i++)
    {
        if (!music_voices[i].active)
        {
            use_idx = i;
            break;
        }
    }

    if (use_idx < 0)
    {
        unsigned long oldest_age;
        oldest_age = music_voices[0].age;
        use_idx = 0;
        for (i = 1; i < MIDI_MAX_VOICES; i++)
        {
            if (music_voices[i].age < oldest_age)
            {
                oldest_age = music_voices[i].age;
                use_idx = i;
            }
        }
    }

    music_voices[use_idx].active = 1;
    music_voices[use_idx].channel = ev->channel;
    music_voices[use_idx].note = ev->a;
    music_voices[use_idx].level = max(64, ((int)ev->b * 1800) / 127);
    music_voices[use_idx].phase = 0;
    music_voices[use_idx].step = music_note_step[ev->a & 0x7f];
    music_voices[use_idx].age = music_voice_age++;
}

static void music_reset_playback_locked(void)
{
    music_event_index = 0;
    music_samples_played = 0;
    music_tick_fp = 0;
    music_tempo_us = MIDI_DEFAULT_TEMPO_US;
    music_update_timing_locked();
    music_clear_voices_locked();

    while (music_event_index < music_event_count && music_events[music_event_index].tick == 0)
    {
        music_apply_event_locked(&music_events[music_event_index]);
        music_event_index++;
    }
}

static int music_render_sample_locked(void)
{
    int i;
    int sample;
    unsigned long current_tick;

#if !FX_ENABLE_MUSIC_SYNTH
    return 0;
#endif

    if (!music_playing || music_paused)
        return 0;

    current_tick = music_tick_fp >> MUSIC_TICK_FP_SHIFT;
    while (music_event_index < music_event_count &&
           music_events[music_event_index].tick <= current_tick)
    {
        music_apply_event_locked(&music_events[music_event_index]);
        music_event_index++;
    }

    if (music_event_index >= music_event_count)
    {
        if (music_loopflag == MUSIC_LoopSong)
        {
            music_reset_playback_locked();
        }
        else
        {
            music_playing = 0;
            music_clear_voices_locked();
            return 0;
        }
    }

    sample = 0;
    for (i = 0; i < MIDI_MAX_VOICES; i++)
    {
        if (music_voices[i].active)
        {
            music_voices[i].phase += music_voices[i].step;
            sample += (music_voices[i].phase & 0x8000U) ? -music_voices[i].level : music_voices[i].level;
        }
    }

    music_tick_fp += music_ticks_per_sample_fp;
    music_samples_played++;

    if (music_volume <= 0)
        return 0;

    sample = (sample * music_volume) / 255;
    return sample;
}

static void fx_stop_voice_locked(int index, int queue_callback)
{
    if (index < 0 || index >= fx_num_voices)
        return;

    if (!fx_voices[index].active)
        return;

    if (queue_callback)
        fx_queue_callback_locked(fx_voices[index].callbackval);

    if (fx_voices[index].samples != NULL)
    {
        if (fx_in_audio_callback)
            fx_queue_sample_free_locked(fx_voices[index].samples);
        else
            free(fx_voices[index].samples);
        fx_voices[index].samples = NULL;
    }

    memset(&fx_voices[index], 0, sizeof(fx_voices[index]));
}

static int fx_grab_voice_locked(int priority)
{
    int i;
    int replace_idx;
    unsigned long oldest;

    for (i = 0; i < fx_num_voices; i++)
    {
        if (!fx_voices[i].active)
            return i;
    }

    replace_idx = 0;
    oldest = fx_voices[0].birthday;
    for (i = 1; i < fx_num_voices; i++)
    {
        if (fx_voices[i].priority > priority)
            continue;
        if (fx_voices[i].birthday < oldest)
        {
            oldest = fx_voices[i].birthday;
            replace_idx = i;
        }
    }

    fx_stop_voice_locked(replace_idx, 1);
    return replace_idx;
}

static int fx_find_voice_locked(int handle)
{
    int i;

    for (i = 0; i < fx_num_voices; i++)
    {
        if (fx_voices[i].active && fx_voices[i].handle == handle)
            return i;
    }

    return -1;
}

static void fx_audio_callback(void *userdata, Uint8 *stream, int len)
{
    Sint16 *out;
    int frames;
    int frame;
    int i;

    (void)userdata;

    out = (Sint16 *)stream;
    frames = len / (int)(sizeof(Sint16) * 2);
    memset(stream, 0, (size_t)len);
    fx_in_audio_callback = 1;

    for (frame = 0; frame < frames; frame++)
    {
        int mix_l;
        int mix_r;
        int music_sample;

        mix_l = 0;
        mix_r = 0;

        for (i = 0; i < fx_num_voices; i++)
        {
            fx_voice_t *v;
            int idx;
            int s;
            int gl;
            int gr;

            v = &fx_voices[i];
            if (!v->active || v->samples == NULL)
                continue;

            idx = v->pos_fp >> 16;
            if (idx >= v->frames)
            {
                fx_stop_voice_locked(i, 1);
                continue;
            }

            s = v->samples[idx];
            gl = (v->left * v->vol) / 255;
            gr = (v->right * v->vol) / 255;
            gl = (gl * fx_global_volume) / 255;
            gr = (gr * fx_global_volume) / 255;

            mix_l += (s * gl) / 255;
            mix_r += (s * gr) / 255;

            v->pos_fp += v->step_fp;
            if ((v->pos_fp >> 16) >= v->frames)
                fx_stop_voice_locked(i, 1);
        }

        music_sample = music_render_sample_locked();
        mix_l += music_sample;
        mix_r += music_sample;

#if FX_MIX_ATTENUATION_SHIFT > 0
        mix_l >>= FX_MIX_ATTENUATION_SHIFT;
        mix_r >>= FX_MIX_ATTENUATION_SHIFT;
#endif

        if (fx_reverse_stereo)
        {
            int t;
            t = mix_l;
            mix_l = mix_r;
            mix_r = t;
        }

        mix_l = max(-32768, min(32767, mix_l));
        mix_r = max(-32768, min(32767, mix_r));

        out[frame * 2] = (Sint16)mix_l;
        out[frame * 2 + 1] = (Sint16)mix_r;
    }

    fx_in_audio_callback = 0;
}

static void fx_pump_callbacks(void)
{
    unsigned long local_queue[FX_CALLBACK_QUEUE];
    int local_count;

    fx_flush_sample_free_queue();

    if (fx_callback == NULL)
        return;

    local_count = 0;

    SDL_LockAudio();
    while (fx_cb_read != fx_cb_write && local_count < FX_CALLBACK_QUEUE)
    {
        local_queue[local_count++] = fx_cb_queue[fx_cb_read];
        fx_cb_read = (fx_cb_read + 1) % FX_CALLBACK_QUEUE;
    }
    SDL_UnlockAudio();

    while (local_count > 0)
    {
        local_count--;
        fx_callback(local_queue[local_count]);
    }
}

static int fx_ensure_audio(int mixrate)
{
    SDL_AudioSpec want;
    SDL_AudioSpec got;
    Uint32 flags;

    if (fx_audio_opened)
        return 1;

    flags = SDL_WasInit(SDL_INIT_AUDIO);
    if ((flags & SDL_INIT_AUDIO) == 0)
    {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0)
        {
            fx_set_error(SDL_GetError());
            return 0;
        }
        fx_audio_owned = 1;
    }

    memset(&want, 0, sizeof(want));
#if defined(__MINT__)
    if (mixrate <= 0 || mixrate > FX_DEFAULT_RATE)
        mixrate = FX_DEFAULT_RATE;
#endif
    want.freq = (mixrate > 0) ? mixrate : FX_DEFAULT_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = FX_BUFFER_SAMPLES;
    want.callback = fx_audio_callback;

    if (SDL_OpenAudio(&want, &got) < 0)
    {
        fx_set_error(SDL_GetError());
        if (fx_audio_owned)
        {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            fx_audio_owned = 0;
        }
        return 0;
    }

    fx_mix_rate = (got.freq > 0) ? got.freq : want.freq;
    fx_audio_opened = 1;
    music_update_timing_locked();
    music_build_note_steps_locked();
    SDL_PauseAudio(0);

    return 1;
}

static int fx_calc_3d_values(int angle, int distance, int *vol, int *left, int *right)
{
    int v;
    int dist;

    dist = distance;
    if (dist < 0)
    {
        dist = -dist;
        angle += MV_NumPanPositions / 2;
    }

    dist = max(0, min(255, dist));
    angle &= MV_MaxPanPosition;

    v = MIX_VOLUME(dist);

    *vol = max(0, 255 - dist);
    *left = MV_PanTable[angle][v].left;
    *right = MV_PanTable[angle][v].right;

    return 1;
}

static int fx_start_voice(char *ptr, int size, int pitchoffset,
                          int vol, int left, int right,
                          int priority, unsigned long callbackval)
{
    decoded_sample_t decoded;
    int idx;
    int handle;
    int base_step_fp;
    int step_fp;

    if (!FX_Installed)
    {
        FX_ErrorCode = FX_Error;
        fx_set_error("Sound system is not initialized");
        return FX_Error;
    }

    if (size <= 0)
    {
        FX_ErrorCode = FX_Error;
        fx_set_error("Invalid sample length");
        return FX_Error;
    }

    if (!fx_decode_sample_data((const unsigned char *)ptr, size, &decoded))
    {
        FX_ErrorCode = FX_Error;
        fx_set_error("Unsupported or invalid sound data");
        return FX_Error;
    }

    base_step_fp = (int)(((long)decoded.rate << 16) / max(1, fx_mix_rate));
    step_fp = fx_scale_step_by_cents(base_step_fp, pitchoffset);

    SDL_LockAudio();

    idx = fx_grab_voice_locked(priority);
    handle = fx_next_handle++;
    if (fx_next_handle <= 0)
        fx_next_handle = FX_HANDLE_START;

    fx_voices[idx].active = 1;
    fx_voices[idx].handle = handle;
    fx_voices[idx].priority = priority;
    fx_voices[idx].birthday = (unsigned long)SDL_GetTicks();
    fx_voices[idx].callbackval = callbackval;
    fx_voices[idx].samples = decoded.samples;
    fx_voices[idx].frames = decoded.frames;
    fx_voices[idx].pos_fp = 0;
    fx_voices[idx].base_step_fp = base_step_fp;
    fx_voices[idx].step_fp = step_fp;
    fx_voices[idx].vol = max(0, min(255, vol));
    fx_voices[idx].left = max(0, min(255, left));
    fx_voices[idx].right = max(0, min(255, right));

    SDL_UnlockAudio();

    FX_ErrorCode = FX_Ok;
    fx_pump_callbacks();
    return handle;
}

static int fx_guess_size(unsigned long callbackval)
{
    int sndnum;
    int lump;

    sndnum = (int)callbackval;
    lump = SoundNumber(sndnum);
    return W_LumpLength(lump);
}

static int midi_builder_push(midi_builder_t *b, const midi_event_t *ev)
{
    midi_event_t *tmp;
    int new_cap;

    if (b->count >= b->cap)
    {
        new_cap = (b->cap > 0) ? (b->cap * 2) : 1024;
        if (new_cap < b->count + 1)
            new_cap = b->count + 1;

        tmp = (midi_event_t *)realloc(b->items, (size_t)new_cap * sizeof(midi_event_t));
        if (tmp == NULL)
            return 0;

        b->items = tmp;
        b->cap = new_cap;
    }

    b->items[b->count++] = *ev;
    return 1;
}

static int midi_read_varlen(const unsigned char *data, int size, int *pos, unsigned long *out)
{
    unsigned long value;
    int count;
    unsigned char c;

    value = 0;
    count = 0;

    do
    {
        if (*pos >= size || count >= 4)
            return 0;

        c = data[(*pos)++];
        value = (value << 7) | (unsigned long)(c & 0x7F);
        count++;
    } while (c & 0x80);

    *out = value;
    return 1;
}

static int midi_parse_track(const unsigned char *data, int size, midi_builder_t *builder)
{
    int pos;
    unsigned long tick;
    unsigned char running;

    pos = 0;
    tick = 0;
    running = 0;

    while (pos < size)
    {
        unsigned long delta;
        unsigned char status;

        if (!midi_read_varlen(data, size, &pos, &delta))
            return 0;
        tick += delta;

        if (pos >= size)
            return 0;

        status = data[pos++];

        if (status < 0x80)
        {
            if (running == 0)
                return 0;
            pos--;
            status = running;
        }
        else if (status < 0xF0)
        {
            running = status;
        }
        else
        {
            running = 0;
        }

        if (status == 0xFF)
        {
            unsigned char meta;
            unsigned long meta_len;

            if (pos >= size)
                return 0;
            meta = data[pos++];

            if (!midi_read_varlen(data, size, &pos, &meta_len))
                return 0;
            if (meta_len > (unsigned long)(size - pos))
                return 0;

            if (meta == 0x51 && meta_len == 3)
            {
                midi_event_t ev;
                memset(&ev, 0, sizeof(ev));
                ev.tick = tick;
                ev.order = builder->order_counter++;
                ev.type = MIDI_EVENT_TEMPO;
                ev.tempo_us = fx_read_be24(data + pos);
                if (!midi_builder_push(builder, &ev))
                    return 0;
            }
            else if (meta == 0x2F)
            {
                return 1;
            }

            pos += (int)meta_len;
            continue;
        }

        if (status == 0xF0 || status == 0xF7)
        {
            unsigned long syx_len;

            if (!midi_read_varlen(data, size, &pos, &syx_len))
                return 0;
            if (syx_len > (unsigned long)(size - pos))
                return 0;
            pos += (int)syx_len;
            continue;
        }

        {
            unsigned char kind;
            unsigned char channel;
            unsigned char d1;
            unsigned char d2;

            kind = status & 0xF0;
            channel = status & 0x0F;

            if (pos >= size)
                return 0;
            d1 = data[pos++];
            d2 = 0;

            if (kind != 0xC0 && kind != 0xD0)
            {
                if (pos >= size)
                    return 0;
                d2 = data[pos++];
            }

            if (kind == 0x80 || kind == 0x90)
            {
                midi_event_t ev;

                memset(&ev, 0, sizeof(ev));
                ev.tick = tick;
                ev.order = builder->order_counter++;
                ev.channel = channel;
                ev.a = d1;
                ev.b = d2;

                if (kind == 0x80 || d2 == 0)
                    ev.type = MIDI_EVENT_NOTE_OFF;
                else
                    ev.type = MIDI_EVENT_NOTE_ON;

                if (!midi_builder_push(builder, &ev))
                    return 0;
            }
        }
    }

    return 1;
}

static int midi_event_cmp(const void *a, const void *b)
{
    const midi_event_t *ea;
    const midi_event_t *eb;

    ea = (const midi_event_t *)a;
    eb = (const midi_event_t *)b;

    if (ea->tick < eb->tick)
        return -1;
    if (ea->tick > eb->tick)
        return 1;
    if (ea->order < eb->order)
        return -1;
    if (ea->order > eb->order)
        return 1;
    return 0;
}

static void music_free_events(void)
{
    if (music_events != NULL)
    {
        free(music_events);
        music_events = NULL;
    }
    music_event_count = 0;
    music_event_index = 0;
}

static int music_parse_midi(const unsigned char *song, int size)
{
    unsigned long hdr_len;
    unsigned short tracks;
    unsigned short division;
    int pos;
    unsigned short parsed_tracks;
    midi_builder_t builder;

    if (song == NULL || size < 14)
        return 0;

    if (memcmp(song, "MThd", 4) != 0)
        return 0;

    hdr_len = fx_read_be32(song + 4);
    if (hdr_len < 6 || hdr_len > (unsigned long)(size - 8))
        return 0;

    tracks = fx_read_be16(song + 10);
    division = fx_read_be16(song + 12);

    if (division & 0x8000)
        return 0;

    builder.items = NULL;
    builder.count = 0;
    builder.cap = 0;
    builder.order_counter = 0;

    pos = 8 + (int)hdr_len;
    parsed_tracks = 0;

    while (pos + 8 <= size && parsed_tracks < tracks)
    {
        unsigned long trk_len;
        int trk_end;

        if (memcmp(song + pos, "MTrk", 4) != 0)
            break;

        trk_len = fx_read_be32(song + pos + 4);
        if (trk_len > (unsigned long)(size - pos - 8))
        {
            free(builder.items);
            return 0;
        }

        trk_end = pos + 8 + (int)trk_len;
        if (!midi_parse_track(song + pos + 8, (int)trk_len, &builder))
        {
            free(builder.items);
            return 0;
        }

        pos = trk_end;
        parsed_tracks++;
    }

    if (builder.count <= 0)
    {
        free(builder.items);
        return 0;
    }

    qsort(builder.items, (size_t)builder.count, sizeof(midi_event_t), midi_event_cmp);

    music_free_events();
    music_events = builder.items;
    music_event_count = builder.count;
    music_division = (division > 0) ? division : MIDI_DEFAULT_DIVISION;

    return 1;
}

char *FX_ErrorString(int ErrorNumber)
{
    switch (ErrorNumber)
    {
        case FX_Warning:
            return fx_warning_message[0] ? fx_warning_message : "Audio warning";
        case FX_Error:
            return fx_error_message[0] ? fx_error_message : "Audio error";
        case FX_Ok:
            return "OK; no error.";
        case FX_ASSVersion:
            return "Incorrect sound library version.";
        case FX_BlasterError:
            return "SoundBlaster error.";
        case FX_SoundCardError:
            return "General sound card error.";
        case FX_InvalidCard:
            return "Invalid sound card.";
        case FX_MultiVocError:
            return "Sound mixer error.";
        case FX_DPMI_Error:
            return "Memory lock error.";
        default:
            return "Unknown audio error.";
    }
}

int FX_SetupCard(int SoundCard, fx_device *device)
{
    FX_SoundDevice = SoundCard;

    if (SoundCard != SoundScape)
    {
        FX_ErrorCode = FX_InvalidCard;
        fx_set_error("Only SoundScape-compatible mode is supported");
        return FX_Error;
    }

    if (device != NULL)
    {
        device->MaxVoices = 8;
        device->MaxSampleBits = 16;
        device->MaxChannels = 2;
    }

    FX_ErrorCode = FX_Ok;
    return FX_Ok;
}

int FX_Init(int SoundCard, int numvoices, int numchannels, int samplebits, unsigned mixrate)
{
    int i;

    (void)numchannels;
    (void)samplebits;

    if (SoundCard != SoundScape)
    {
        FX_ErrorCode = FX_InvalidCard;
        fx_set_error("Only SoundScape-compatible mode is supported");
        return FX_Error;
    }

    if (!fx_ensure_audio((int)mixrate))
    {
        FX_ErrorCode = FX_Error;
        return FX_Error;
    }

    MV_CalcPanTable();

    fx_num_voices = min(FX_MAX_VOICES, max(1, min(numvoices, FX_PLATFORM_MAX_VOICES)));
    fx_next_handle = FX_HANDLE_START;
    fx_cb_read = 0;
    fx_cb_write = 0;
    fx_free_read = 0;
    fx_free_write = 0;

    SDL_LockAudio();
    for (i = 0; i < FX_MAX_VOICES; i++)
    {
        if (fx_voices[i].samples != NULL)
            free(fx_voices[i].samples);
        memset(&fx_voices[i], 0, sizeof(fx_voices[i]));
    }
    music_update_timing_locked();
    SDL_UnlockAudio();

    FX_Installed = 1;
    FX_SoundDevice = SoundCard;
    FX_ErrorCode = FX_Ok;
    return FX_Ok;
}

int FX_Shutdown(void)
{
    int i;

    fx_pump_callbacks();

    if (!FX_Installed)
    {
        FX_ErrorCode = FX_Error;
        fx_set_error("Sound system is not currently initialized");
        return FX_Error;
    }

    SDL_LockAudio();
    for (i = 0; i < FX_MAX_VOICES; i++)
    {
        fx_stop_voice_locked(i, 0);
    }
    music_playing = 0;
    music_paused = 0;
    music_clear_voices_locked();
    SDL_UnlockAudio();
    fx_flush_sample_free_queue();

    if (fx_audio_opened)
    {
        SDL_CloseAudio();
        fx_audio_opened = 0;
    }

    if (fx_audio_owned)
    {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        fx_audio_owned = 0;
    }

    FX_Installed = 0;
    FX_ErrorCode = FX_Ok;
    return FX_Ok;
}

int FX_SetCallBack(void (*function)(unsigned long))
{
    fx_callback = function;
    return FX_Ok;
}

void FX_SetVolume(int volume)
{
    fx_global_volume = max(0, min(255, volume));
}

int FX_GetVolume(void)
{
    return fx_global_volume;
}

void FX_SetReverseStereo(int setting)
{
    fx_reverse_stereo = setting ? 1 : 0;
}

int FX_GetReverseStereo(void)
{
    return fx_reverse_stereo;
}

void FX_SetReverb(int reverb)
{
    fx_reverb = reverb;
}

void FX_SetFastReverb(int reverb)
{
    fx_fast_reverb = reverb;
}

int FX_GetMaxReverbDelay(void)
{
    return max(256, fx_mix_rate);
}

int FX_GetReverbDelay(void)
{
    return fx_reverb_delay;
}

void FX_SetReverbDelay(int delay)
{
    fx_reverb_delay = max(256, delay);
}

int FX_VoiceAvailable(int priority)
{
    int i;

    fx_pump_callbacks();

    if (!FX_Installed)
        return 0;

    SDL_LockAudio();
    for (i = 0; i < fx_num_voices; i++)
    {
        if (!fx_voices[i].active)
        {
            SDL_UnlockAudio();
            return 1;
        }
        if (fx_voices[i].priority <= priority)
        {
            SDL_UnlockAudio();
            return 1;
        }
    }
    SDL_UnlockAudio();

    return 0;
}

int FX_EndLooping(int handle)
{
    (void)handle;
    return FX_Ok;
}

int FX_SetPan(int handle, int vol, int left, int right)
{
    int idx;

    fx_pump_callbacks();

    SDL_LockAudio();
    idx = fx_find_voice_locked(handle);
    if (idx >= 0)
    {
        fx_voices[idx].vol = max(0, min(255, vol));
        fx_voices[idx].left = max(0, min(255, left));
        fx_voices[idx].right = max(0, min(255, right));
        SDL_UnlockAudio();
        return FX_Ok;
    }
    SDL_UnlockAudio();

    FX_ErrorCode = FX_Warning;
    fx_set_warning("Invalid handle in FX_SetPan");
    return FX_Warning;
}

int FX_SetPitch(int handle, int pitchoffset)
{
    int idx;

    fx_pump_callbacks();

    SDL_LockAudio();
    idx = fx_find_voice_locked(handle);
    if (idx >= 0)
    {
        fx_voices[idx].step_fp = fx_scale_step_by_cents(fx_voices[idx].base_step_fp, pitchoffset);
        SDL_UnlockAudio();
        return FX_Ok;
    }
    SDL_UnlockAudio();

    FX_ErrorCode = FX_Warning;
    fx_set_warning("Invalid handle in FX_SetPitch");
    return FX_Warning;
}

int FX_SetFrequency(int handle, int frequency)
{
    int idx;
    int step_fp;

    fx_pump_callbacks();

    if (frequency <= 0)
        return FX_Error;

    step_fp = (int)(((long)frequency << 16) / max(1, fx_mix_rate));
    if (step_fp < 256)
        step_fp = 256;

    SDL_LockAudio();
    idx = fx_find_voice_locked(handle);
    if (idx >= 0)
    {
        fx_voices[idx].base_step_fp = step_fp;
        fx_voices[idx].step_fp = step_fp;
        SDL_UnlockAudio();
        return FX_Ok;
    }
    SDL_UnlockAudio();

    FX_ErrorCode = FX_Warning;
    fx_set_warning("Invalid handle in FX_SetFrequency");
    return FX_Warning;
}

int FX_PlayVOC(char *ptr, int pitchoffset, int vol, int left, int right,
       int priority, unsigned long callbackval)
{
    int size;

    size = fx_guess_size(callbackval);
    return fx_start_voice(ptr, size, pitchoffset, vol, left, right, priority, callbackval);
}

int FX_PlayLoopedVOC(char *ptr, long loopstart, long loopend,
       int pitchoffset, int vol, int left, int right, int priority,
       unsigned long callbackval)
{
    (void)loopstart;
    (void)loopend;
    return FX_PlayVOC(ptr, pitchoffset, vol, left, right, priority, callbackval);
}

int FX_PlayWAV(char *ptr, int pitchoffset, int vol, int left, int right,
       int priority, unsigned long callbackval)
{
    int size;

    size = fx_guess_size(callbackval);
    return fx_start_voice(ptr, size, pitchoffset, vol, left, right, priority, callbackval);
}

int FX_PlayLoopedWAV(char *ptr, long loopstart, long loopend,
       int pitchoffset, int vol, int left, int right, int priority,
       unsigned long callbackval)
{
    (void)loopstart;
    (void)loopend;
    return FX_PlayWAV(ptr, pitchoffset, vol, left, right, priority, callbackval);
}

int FX_PlayVOC3D(char *ptr, int pitchoffset, int angle, int distance,
       int priority, unsigned long callbackval)
{
    int vol;
    int left;
    int right;
    int size;

    fx_calc_3d_values(angle, distance, &vol, &left, &right);
    size = fx_guess_size(callbackval);
    return fx_start_voice(ptr, size, pitchoffset, vol, left, right, priority, callbackval);
}

int FX_PlayVOC3D_ROTT(char *ptr, int size, int pitchoffset, int angle, int distance,
       int priority, unsigned long callbackval)
{
    int vol;
    int left;
    int right;

    fx_calc_3d_values(angle, distance, &vol, &left, &right);
    return fx_start_voice(ptr, size, pitchoffset, vol, left, right, priority, callbackval);
}

int FX_PlayWAV3D(char *ptr, int pitchoffset, int angle, int distance,
       int priority, unsigned long callbackval)
{
    int vol;
    int left;
    int right;
    int size;

    fx_calc_3d_values(angle, distance, &vol, &left, &right);
    size = fx_guess_size(callbackval);
    return fx_start_voice(ptr, size, pitchoffset, vol, left, right, priority, callbackval);
}

int FX_PlayWAV3D_ROTT(char *ptr, int size, int pitchoffset, int angle, int distance,
       int priority, unsigned long callbackval)
{
    int vol;
    int left;
    int right;

    fx_calc_3d_values(angle, distance, &vol, &left, &right);
    return fx_start_voice(ptr, size, pitchoffset, vol, left, right, priority, callbackval);
}

int FX_PlayRaw(char *ptr, unsigned long length, unsigned rate,
       int pitchoffset, int vol, int left, int right, int priority,
       unsigned long callbackval)
{
    return fx_start_voice(ptr, (int)length, pitchoffset, vol, left, right, priority, callbackval);
}

int FX_PlayLoopedRaw(char *ptr, unsigned long length, char *loopstart,
       char *loopend, unsigned rate, int pitchoffset, int vol, int left,
       int right, int priority, unsigned long callbackval)
{
    (void)loopstart;
    (void)loopend;
    (void)rate;
    return fx_start_voice(ptr, (int)length, pitchoffset, vol, left, right, priority, callbackval);
}

int FX_Pan3D(int handle, int angle, int distance)
{
    int vol;
    int left;
    int right;

    fx_calc_3d_values(angle, distance, &vol, &left, &right);
    return FX_SetPan(handle, vol, left, right);
}

int FX_SoundActive(int handle)
{
    int idx;

    fx_pump_callbacks();

    SDL_LockAudio();
    idx = fx_find_voice_locked(handle);
    SDL_UnlockAudio();

    return (idx >= 0) ? 1 : 0;
}

int FX_SoundsPlaying(void)
{
    int i;
    int playing;

    fx_pump_callbacks();

    playing = 0;
    SDL_LockAudio();
    for (i = 0; i < fx_num_voices; i++)
    {
        if (fx_voices[i].active)
        {
            playing++;
        }
    }
    SDL_UnlockAudio();

    return playing;
}

int FX_StopSound(int handle)
{
    int idx;

    fx_pump_callbacks();

    SDL_LockAudio();
    idx = fx_find_voice_locked(handle);
    if (idx >= 0)
    {
        fx_stop_voice_locked(idx, 1);
        SDL_UnlockAudio();
        fx_pump_callbacks();
        return FX_Ok;
    }
    SDL_UnlockAudio();

    FX_ErrorCode = FX_Warning;
    fx_set_warning("Invalid handle in FX_StopSound");
    return FX_Warning;
}

int FX_StopAllSounds(void)
{
    int i;

    fx_pump_callbacks();

    SDL_LockAudio();
    for (i = 0; i < fx_num_voices; i++)
    {
        fx_stop_voice_locked(i, 1);
    }
    SDL_UnlockAudio();

    fx_pump_callbacks();
    return FX_Ok;
}

int FX_StartDemandFeedPlayback(void (*function)(char **ptr, unsigned long *length),
       int rate, int pitchoffset, int vol, int left, int right,
       int priority, unsigned long callbackval)
{
    (void)function;
    (void)rate;
    (void)pitchoffset;
    (void)vol;
    (void)left;
    (void)right;
    (void)priority;
    (void)callbackval;
    FX_ErrorCode = FX_Warning;
    fx_set_warning("Demand feed playback is unavailable");
    return 0;
}

int FX_StartRecording(int MixRate, void (*function)(char *ptr, int length))
{
    (void)MixRate;
    (void)function;
    FX_ErrorCode = FX_Warning;
    fx_set_warning("Recording is not implemented");
    return FX_Warning;
}

void FX_StopRecord(void)
{
}

char *MUSIC_ErrorString(int ErrorNumber)
{
    switch (ErrorNumber)
    {
        case MUSIC_Warning:
            return music_warning_message[0] ? music_warning_message : "Music warning";
        case MUSIC_Error:
            return music_error_message[0] ? music_error_message : "Music error";
        case MUSIC_Ok:
            return "OK; no error.";
        case MUSIC_ASSVersion:
            return "Incorrect sound library version.";
        case MUSIC_SoundCardError:
            return "General music device error.";
        case MUSIC_MPU401Error:
            return "MPU-401 error.";
        case MUSIC_InvalidCard:
            return "Invalid music device.";
        case MUSIC_MidiError:
            return "MIDI playback error.";
        case MUSIC_TaskManError:
            return "Task manager error.";
        case MUSIC_FMNotDetected:
            return "FM device not detected.";
        case MUSIC_DPMI_Error:
            return "Memory lock error.";
        default:
            return "Unknown music error.";
    }
}

int MUSIC_Init(int SoundCard, int Address)
{
    int was_installed;

    (void)Address;

    if (SoundCard != SoundScape)
    {
        MUSIC_ErrorCode = MUSIC_InvalidCard;
        music_set_error("Only SoundScape-compatible mode is supported");
        return MUSIC_Error;
    }

    was_installed = FX_Installed;
    if (!FX_Installed)
    {
        if (FX_Init(SoundScape, 8, StereoFx, 16, FX_DEFAULT_RATE) != FX_Ok)
        {
            MUSIC_ErrorCode = MUSIC_SoundCardError;
            music_set_error(FX_ErrorString(FX_ErrorCode));
            return MUSIC_Error;
        }
        music_owns_fx = 1;
    }
    else
    {
        music_owns_fx = 0;
    }

    if (!was_installed)
        FX_SetVolume(fx_global_volume);

    music_initialized = 1;
    music_loopflag = MUSIC_PlayOnce;
    music_playing = 0;
    music_paused = 0;
    music_fade_active = 0;
    music_context = 0;
    music_samples_played = 0;
    music_voice_age = 1;

    MUSIC_ErrorCode = MUSIC_Ok;
    return MUSIC_Ok;
}

int MUSIC_Shutdown(void)
{
    if (!music_initialized)
    {
        MUSIC_ErrorCode = MUSIC_Error;
        music_set_error("Music system is not currently initialized");
        return MUSIC_Error;
    }

    MUSIC_StopSong();
    music_free_events();
    music_initialized = 0;

    if (music_owns_fx)
    {
        FX_Shutdown();
        music_owns_fx = 0;
    }

    MUSIC_ErrorCode = MUSIC_Ok;
    return MUSIC_Ok;
}

void MUSIC_SetMaxFMMidiChannel(int channel)
{
    (void)channel;
}

void MUSIC_SetVolume(int volume)
{
    music_volume = max(0, min(255, volume));
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
#if !FX_ENABLE_MUSIC_SYNTH
    return 0;
#endif
    return (music_playing && !music_paused) ? 1 : 0;
}

void MUSIC_Continue(void)
{
    if (!music_initialized)
        return;

#if !FX_ENABLE_MUSIC_SYNTH
    return;
#endif

    if (music_paused)
    {
        music_paused = 0;
        return;
    }

    if (music_event_count > 0 && !music_playing)
    {
        SDL_LockAudio();
        music_reset_playback_locked();
        music_playing = 1;
        music_paused = 0;
        SDL_UnlockAudio();
    }
}

void MUSIC_Pause(void)
{
#if !FX_ENABLE_MUSIC_SYNTH
    return;
#endif
    music_paused = 1;
}

int MUSIC_StopSong(void)
{
    if (!music_initialized)
        return MUSIC_Error;

    SDL_LockAudio();
    music_playing = 0;
    music_paused = 0;
    music_samples_played = 0;
    music_tick_fp = 0;
    music_event_index = 0;
    music_clear_voices_locked();
    SDL_UnlockAudio();

    return MUSIC_Ok;
}

int MUSIC_PlaySong(unsigned char *song, int loopflag)
{
    (void)song;
    (void)loopflag;
    MUSIC_ErrorCode = MUSIC_MidiError;
    music_set_error("Use MUSIC_PlaySongROTT for in-memory song data");
    return MUSIC_Error;
}

int MUSIC_PlaySongROTT(unsigned char *song, int size, int loopflag)
{
    if (!music_initialized)
    {
        MUSIC_ErrorCode = MUSIC_Error;
        music_set_error("Music system is not initialized");
        return MUSIC_Error;
    }

#if !FX_ENABLE_MUSIC_SYNTH
    (void)song;
    (void)size;
    (void)loopflag;
    SDL_LockAudio();
    music_playing = 0;
    music_paused = 0;
    music_samples_played = 0;
    music_tick_fp = 0;
    music_event_index = 0;
    music_clear_voices_locked();
    SDL_UnlockAudio();
    MUSIC_ErrorCode = MUSIC_Ok;
    return MUSIC_Ok;
#else
    if (!music_parse_midi(song, size))
    {
        MUSIC_ErrorCode = MUSIC_MidiError;
        music_set_error("Invalid or unsupported MIDI data");
        return MUSIC_Error;
    }

    SDL_LockAudio();
    music_loopflag = loopflag;
    music_reset_playback_locked();
    music_playing = 1;
    music_paused = 0;
    SDL_UnlockAudio();

    MUSIC_ErrorCode = MUSIC_Ok;
    return MUSIC_Ok;
#endif
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
    (void)PositionInTicks;
}

void MUSIC_SetSongTime(unsigned long milliseconds)
{
    (void)milliseconds;
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

    SDL_LockAudio();
    pos->tickposition = (music_tick_fp >> MUSIC_TICK_FP_SHIFT);
    pos->milliseconds = (music_samples_played * 1000UL) / (unsigned long)max(1, fx_mix_rate);
    pos->measure = 0;
    pos->beat = 0;
    pos->tick = (unsigned int)(music_tick_fp >> MUSIC_TICK_FP_SHIFT);
    SDL_UnlockAudio();
}

void MUSIC_GetSongLength(songposition *pos)
{
    if (pos == NULL)
        return;

    pos->tickposition = 0;
    pos->milliseconds = 0;
    pos->measure = 0;
    pos->beat = 0;
    pos->tick = 0;
}

int MUSIC_FadeVolume(int tovolume, int milliseconds)
{
    (void)milliseconds;
    music_fade_active = 0;
    music_volume = max(0, min(255, tovolume));
    return MUSIC_Ok;
}

int MUSIC_FadeActive(void)
{
    return music_fade_active;
}

void MUSIC_StopFade(void)
{
    music_fade_active = 0;
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
