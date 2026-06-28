/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdlib.h>
#include <string.h>

#include "dsl.h"
#include "util.h"

#if defined(__MINT__)
#include <mint/osbind.h>
#include <mint/cookie.h>

// Atari STE DMA sound hardware registers
static volatile unsigned char *pDmaSndCtrl = (void *)0xff8901;
static volatile unsigned char *pDmaSndBasHi = (void *)0xff8903;
static volatile unsigned char *pDmaSndBasMi = (void *)0xff8905;
static volatile unsigned char *pDmaSndBasLo = (void *)0xff8907;
static volatile unsigned char *pDmaSndAdrHi = (void *)0xff8909;
static volatile unsigned char *pDmaSndAdrMi = (void *)0xff890b;
static volatile unsigned char *pDmaSndAdrLo = (void *)0xff890d;
static volatile unsigned char *pDmaSndEndHi = (void *)0xff890f;
static volatile unsigned char *pDmaSndEndMi = (void *)0xff8911;
static volatile unsigned char *pDmaSndEndLo = (void *)0xff8913;
static volatile unsigned char *pDmaSndMode = (void *)0xff8921;

static void dsl_super_begin(long *old)
{
    *old = Super(0);
}

static void dsl_super_end(long old)
{
    Super(old);
}

#define DMASND_CTRL_OFF 0
#define DMASND_CTRL_ON 1
#define DMASND_CTRL_LOOP 2

#define DMASND_MODE_STEREO 0
#define DMASND_MODE_MONO 128
#define DMASND_MODE_HZ_6258 0
#define DMASND_MODE_HZ_12517 1
#define DMASND_MODE_HZ_25033 2
#define DMASND_MODE_HZ_50066 3

static int DSL_ErrorCode = DSL_Ok;
static void (*_CallBackFunc)(void) = NULL;
static char *_BufferStart = NULL;
static int _BufferSize = 0;
static int _NumDivisions = 0;
static unsigned _SampleRate = 0;
static unsigned _PlaybackRate = 0;
static int _MixMode = 0;
static int _Playing = 0;
static char *_PlayBuffer = NULL;
static int dsl_misses = 0;
static int dsl_calls = 0;
static int dsl_dma_present = -1;
static int dsl_submit_status = 0;

static int dsl_dma_present_check(void);

extern volatile int MV_MixPage;

char *DSL_ErrorString(int ErrorNumber)
{
    char *ErrorString;
    switch (ErrorNumber)
    {
    case DSL_Warning:
    case DSL_Error:
        ErrorString = DSL_ErrorString(DSL_ErrorCode);
        break;
    case DSL_Ok:
        ErrorString = "DMA Driver ok.";
        break;
    case DSL_SDLInitFailure:
        ErrorString = "DMA Audio initialization failed.";
        break;
    case DSL_MixerActive:
        ErrorString = "DMA already initialized.";
        break;
    case DSL_MixerInitFailure:
        ErrorString = "DMA init failure.";
        break;
    default:
        ErrorString = "Unknown DMA error.";
        break;
    }
    return ErrorString;
}

static void DSL_SetErrorCode(int ErrorCode)
{
    DSL_ErrorCode = ErrorCode;
}

int DSL_Init(void)
{
    DSL_SetErrorCode(DSL_Ok);
    if (!dsl_dma_present_check())
    {
        DSL_SetErrorCode(DSL_Error);
        return DSL_Error;
    }
    return DSL_Ok;
}

int DSL_HasDMASound(void)
{
    return dsl_dma_present_check();
}

void DSL_StopPlayback(void)
{
    long olds;
    dsl_super_begin(&olds);
    *pDmaSndCtrl = DMASND_CTRL_OFF;
    dsl_super_end(olds);
    _Playing = 0;
}

unsigned DSL_GetPlaybackRate(void)
{
    return _PlaybackRate;
}

static unsigned dsl_pick_rate(unsigned rate, int bufsize)
{
    unsigned min_rate = (unsigned)bufsize * 25; /* 2 VBLs at 50Hz */
    unsigned max_rate = (unsigned)bufsize * 50; /* 1 VBL at 50Hz */
    unsigned candidates[4] = {6258, 12517, 25033, 50066};
    unsigned best = candidates[0];
    unsigned best_diff = 0xffffffffu;
    int i;
    for (i = 0; i < 4; ++i)
    {
        unsigned c = candidates[i];
        if (c >= min_rate && c <= max_rate)
        {
            unsigned diff = (c > rate) ? (c - rate) : (rate - c);
            if (diff < best_diff)
            {
                best_diff = diff;
                best = c;
            }
        }
    }
    if (best_diff != 0xffffffffu)
        return best;
    /* fallback: closest to requested rate */
    best = candidates[0];
    best_diff = (best > rate) ? (best - rate) : (rate - best);
    for (i = 1; i < 4; ++i)
    {
        unsigned c = candidates[i];
        unsigned diff = (c > rate) ? (c - rate) : (rate - c);
        if (diff < best_diff)
        {
            best_diff = diff;
            best = c;
        }
    }
    return best;
}

static unsigned dsl_pick_mode(unsigned rate)
{
    if (rate >= 40000)
        return DMASND_MODE_HZ_50066;
    if (rate >= 20000)
        return DMASND_MODE_HZ_25033;
    if (rate >= 10000)
        return DMASND_MODE_HZ_12517;
    return DMASND_MODE_HZ_6258;
}

static int dsl_dma_present_check(void)
{
    long cookie = 0;
    if (dsl_dma_present >= 0)
        return dsl_dma_present;
    dsl_dma_present = 0;
    if (C_FOUND == Getcookie(C__SND, &cookie))
    {
        if (cookie != 0)
            dsl_dma_present = 1;
    }
    return dsl_dma_present;
}

static int dsl_should_submit(void)
{
    unsigned long addr = *pDmaSndAdrLo | (*pDmaSndAdrMi << 8) | (*pDmaSndAdrHi << 16);
    int on = *pDmaSndCtrl & DMASND_CTRL_ON;
    dsl_submit_status = 0;
    if (!on || addr == 0 || _PlayBuffer == NULL)
    {
        dsl_submit_status = 1;
        return 1;
    }
    if ((char *)addr < _BufferStart || (char *)addr > (_BufferStart + (_BufferSize * _NumDivisions)))
    {
        dsl_submit_status = 2; /* runaway */
        return 1;
    }
    if (!((char *)addr >= _PlayBuffer && (char *)addr <= _PlayBuffer + _BufferSize))
    {
        dsl_submit_status = 1;
        return 1;
    }
    return 0;
}

static void dsl_submit_buffer(char *buf)
{
    unsigned long addr = (unsigned long)buf;
    *pDmaSndBasLo = (unsigned char)(addr & 0xff);
    *pDmaSndBasMi = (unsigned char)((addr >> 8) & 0xff);
    *pDmaSndBasHi = (unsigned char)((addr >> 16) & 0xff);
    addr += _BufferSize;
    *pDmaSndEndLo = (unsigned char)(addr & 0xff);
    *pDmaSndEndMi = (unsigned char)((addr >> 8) & 0xff);
    *pDmaSndEndHi = (unsigned char)((addr >> 16) & 0xff);
    *pDmaSndCtrl |= DMASND_CTRL_ON | DMASND_CTRL_LOOP;
}

int DSL_BeginBufferedPlayback(char *BufferStart,
                              int BufferSize, int NumDivisions, unsigned SampleRate,
                              int MixMode, void (*CallBackFunc)(void))
{
    _BufferStart = BufferStart;
    _BufferSize = BufferSize;
    _NumDivisions = NumDivisions;
    if (_NumDivisions < 2)
        _NumDivisions = 2;
    if (_NumDivisions > 3)
        _NumDivisions = 3;
    _SampleRate = SampleRate;
    _MixMode = MixMode;
    _CallBackFunc = CallBackFunc;

    _PlaybackRate = dsl_pick_rate(SampleRate, _BufferSize);
    *pDmaSndMode = DMASND_MODE_MONO | dsl_pick_mode(_PlaybackRate);
    *pDmaSndCtrl = DMASND_CTRL_OFF;

    if (_NumDivisions < 2 || _BufferStart == NULL || _CallBackFunc == NULL)
    {
        DSL_SetErrorCode(DSL_Error);
        return DSL_Error;
    }

    _PlayBuffer = _BufferStart;
    MV_MixPage = _NumDivisions - 1;
    _CallBackFunc();
    _PlayBuffer = _BufferStart + (MV_MixPage * _BufferSize);
    dsl_submit_buffer(_PlayBuffer);
    _Playing = 1;
    return DSL_Ok;
}

void DSL_Service(void)
{
    if (!_Playing || _CallBackFunc == NULL)
        return;
    dsl_calls++;
    if (!dsl_should_submit())
        return;

    if (dsl_submit_status == 2)
        dsl_misses++;

    // Mix next buffer and submit it
    // MV_ServiceVoc will advance MV_MixPage to the next buffer.
    _CallBackFunc();
    if (MV_MixPage >= 0 && MV_MixPage < _NumDivisions)
        _PlayBuffer = _BufferStart + (MV_MixPage * _BufferSize);
    dsl_submit_buffer(_PlayBuffer);
}

void DSL_GetDebugStats(int *calls, int *misses)
{
    if (calls)
        *calls = dsl_calls;
    if (misses)
        *misses = dsl_misses;
}

void DSL_Shutdown(void)
{
    DSL_StopPlayback();
}

#else

#include <exec/exec.h>
#include <dos/dos.h>
#include <graphics/gfxbase.h>
#include <devices/audio.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>

#include "doomsound.h"

int numChannels;
int use_libsamplerate = 0;
/**********************************************************************/
#define MAXSFXVOICES 16  /* max number of Sound Effects with server */
#define MAXNUMCHANNELS 4 /* max number of Amiga sound channels */

extern volatile int MV_MixPage;

static int DSL_ErrorCode = DSL_Ok;

static int mixer_initialized = 0;

static void (*_CallBackFunc)(void);
static volatile char *_BufferStart;
static int _BufferSize;
static int _NumDivisions;
static int _SampleRate;
static int _remainder;

struct Library *DoomSndBase = NULL;
static unsigned char *blank_buf;

/*
possible todo ideas: cache sdl/sdl mixer error messages.
*/

char *DSL_ErrorString(int ErrorNumber)
{
    char *ErrorString;

    switch (ErrorNumber)
    {
    case DSL_Warning:
    case DSL_Error:
        ErrorString = DSL_ErrorString(DSL_ErrorCode);
        break;

    case DSL_Ok:
        ErrorString = "SDL Driver ok.";
        break;

    case DSL_SDLInitFailure:
        ErrorString = "SDL Audio initialization failed.";
        break;

    case DSL_MixerActive:
        ErrorString = "SDL Mixer already initialized.";
        break;

    case DSL_MixerInitFailure:
        ErrorString = "SDL Mixer initialization failed.";
        break;

    default:
        ErrorString = "Unknown SDL Driver error.";
        break;
    }

    return ErrorString;
}

static void DSL_SetErrorCode(int ErrorCode)
{
    DSL_ErrorCode = ErrorCode;
}

int DSL_Init(void)
{
    DSL_SetErrorCode(DSL_Ok);

    if ((DoomSndBase = OpenLibrary("doomsound.library", 37)) != NULL)
    {
        Sfx_SetVol(64);
        Mus_SetVol(64);
        numChannels = 4;
    }

    return DSL_Ok;
}

int DSL_HasDMASound(void)
{
    return 1;
}

void DSL_GetDebugStats(int *calls, int *misses)
{
}

void DSL_Shutdown(void)
{
    DSL_StopPlayback();

    if (DoomSndBase != NULL)
    {
        CloseLibrary(DoomSndBase);
        DoomSndBase = NULL;
    }
}

void mixer_callback(void *stream, int len)
{
    unsigned char *stptr;
    unsigned char *fxptr;
    int copysize;

    /* len should equal _BufferSize, else this is screwed up */

    printf("mixer_callback\n");

    stptr = (unsigned char *)stream;

    if (_remainder > 0)
    {
        copysize = min(len, _remainder);

        fxptr = (unsigned char *)(&_BufferStart[MV_MixPage *
                                                _BufferSize]);

        memcpy(stptr, fxptr + (_BufferSize - _remainder), copysize);

        len -= copysize;
        _remainder -= copysize;

        stptr += copysize;
    }

    while (len > 0)
    {
        /* new buffer */

        _CallBackFunc();

        fxptr = (unsigned char *)(&_BufferStart[MV_MixPage *
                                                _BufferSize]);

        copysize = min(len, _BufferSize);

        memcpy(stptr, fxptr, copysize);

        len -= copysize;

        stptr += copysize;
    }

    _remainder = len;
    if (DoomSndBase != NULL)
    {
        Sfx_Start(fxptr, MV_MixPage, 11025,
                  64, 0, _BufferSize);
    }
}

#endif
