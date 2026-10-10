/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * File: md_music.c
 * Description: ROTT's music on the Accelerator (MD_CMD_MUSIC): the ST
 *              build's own player (rott/audiolib/atari_music.c, built with
 *              YMMUSIC_MD), stepped at 50Hz from a timer on Core 1, its
 *              YM2149 registers published at MD_YM_OFFSET for the ST's VBL
 *              to write to the chip. As md-doom's music, which also hands
 *              the ST its registers each VBL, but from ROTT's MIDI songs as
 *              they are rather than files made from them.
 *
 * A song is up to 39KB and there is no RAM for one whole: each MIDI track
 * is read from the WAD through a window, which md_music_service() tops up
 * between commands (a song needs about 200 bytes a second in all). The
 * windows are the 4KB of the USB controller's RAM, unused here, as md-doom
 * keeps its music there (main RAM must leave the boot settings 11KB). The
 * player plays a track's next event only once its window holds all of it
 * (YMMUSIC_TRACK_READY), so a late top-up delays notes, never garbles
 * them. The timer and the main loop share the player under a spinlock.
 * Each step goes into a small ring (MD_YM_SLOTS), which the ST's VBL plays
 * one step at a time.
 *
 * The timer's interrupt is on Core 1, as md-doom found on Core 0 it stalls
 * the renderer; it is held off while flash is written (md_core1_park masks
 * Core 1's interrupts), so the player's code may live in flash.
 */

#include "md_music.h"

#include <string.h>

#include "audiolib/atari_music.h"
#include "debug.h"
#include "hardware/sync.h"
#include "md_core1.h"
#include "md_pack.h"
#include "pico/stdlib.h"
#include "rott_md_protocol.h"
#if PICO_ON_DEVICE
#include "hardware/regs/addressmap.h"
#endif

#define MD_MUSIC_TRACKS 16    /* ROTT's songs have at most 16 */
#define MD_MUSIC_WINDOW 256   /* bytes of each track at a time */
/* What a window must hold past its read point (or the rest of its track)
 * for the next event to play: its longest is 64 bytes and a delta. */
#define MD_MUSIC_MARGIN 128
#define MD_MUSIC_TOPUP 192    /* top up a window with less than this left */
#define MD_MUSIC_STEP_US 20000u
#define MD_MUSIC_TIMER_US 5000

#if PICO_ON_DEVICE
#include "hardware/resets.h"
#define s_win ((uint8_t(*)[MD_MUSIC_WINDOW])USBCTRL_DPRAM_BASE)
_Static_assert(MD_MUSIC_TRACKS * MD_MUSIC_WINDOW <= 4096,
               "the windows are the USB controller's 4KB");
#else
static uint8_t s_win[MD_MUSIC_TRACKS][MD_MUSIC_WINDOW];
#endif
static bool s_ok; /* the windows hold what is written to them */
static uint8_t s_read[MD_MUSIC_WINDOW];
static uint32_t s_first[MD_MUSIC_TRACKS]; /* WAD offset of the track's data */
static uint32_t s_next[MD_MUSIC_TRACKS];  /* of the byte after the window's */
static uint32_t s_end[MD_MUSIC_TRACKS];   /* of the byte after the track */
static int s_tracks;                      /* of the song set up, or 0 */

/* A loop's rewind: the player asks (Core 1), Core 0 refills the windows
 * from the tracks' starts, then the player goes on. Each side only makes
 * its own two moves. */
enum { REWIND_NONE, REWIND_WANTED, REWIND_FILLING, REWIND_DONE };
static volatile int s_rewind;

static spin_lock_t *s_lock;
static volatile uint16_t *s_newest;
static volatile uint16_t *s_slots;
static uint16_t s_step;
static uint64_t s_due;  /* when the next step is */
static alarm_pool_t *s_pool;
static repeating_timer_t s_timer;

static inline int track_index(const ymmusic_midi_track_t *t) {
  return (int)(t - ymmusic_md_track(0));
}

/* ------------------------------------------------------------------ */
/* The player's hooks (Core 1, under the lock)                          */
/* ------------------------------------------------------------------ */

int YMMUSIC_TRACK_READY(const ymmusic_midi_track_t *t) {
  const int i = track_index(t);
  return (t->end - t->ptr) >= MD_MUSIC_MARGIN || s_next[i] >= s_end[i];
}

int ymmusic_md_rewind(void) {
  if (s_rewind == REWIND_DONE) {
    s_rewind = REWIND_NONE;
    return 1;
  }
  if (s_rewind == REWIND_NONE) s_rewind = REWIND_WANTED;
  return 0;
}

/* ------------------------------------------------------------------ */
/* Steps and the registers (Core 1)                                     */
/* ------------------------------------------------------------------ */

/* Step n into slot n % MD_YM_SLOTS: its number 0 while written, then
 * MD_YM_STEP(n); with which registers changed from step n - 1. */
static void publish(void) {
  static uint8_t last[14];
  unsigned changed = 0;

  s_step = (uint16_t)((s_step + 1u) & MD_YM_STEP_MASK);
  volatile uint16_t *slot = s_slots + (s_step % MD_YM_SLOTS) * MD_YM_WORDS;
  slot[MD_YM_SEQ] = 0;
  __dmb();
  for (int r = 0; r < 14; r++) {
    slot[MD_YM_REGS + r] = ymmusic_regs[r];
    if (ymmusic_regs[r] != last[r]) changed |= 1u << r;
    last[r] = ymmusic_regs[r];
  }
  slot[MD_YM_FLAGS] = (uint16_t)((changed << MD_YMF_CHANGED_SHIFT) |
                                 (ymmusic_md_playing() ? MD_YMF_PLAYING : 0));
  __dmb();
  slot[MD_YM_SEQ] = MD_YM_STEP(s_step);
  *s_newest = MD_YM_STEP(s_step);
}

/* Steps by elapsed time, so the tempo holds when the timer is late (Core 1
 * parked while flash is written); four at most, then it lets the rest go. */
static bool timer_cb(repeating_timer_t *rt) {
  (void)rt;
  const uint64_t now = time_us_64();
  int steps = 0;
  const uint32_t irq = spin_lock_blocking(s_lock);
  while (now >= s_due && steps < 4) {
    if (ymmusic_active()) ymmusic_update();
    s_due += MD_MUSIC_STEP_US;
    steps++;
  }
  if (now >= s_due) s_due = now + MD_MUSIC_STEP_US;
  if (steps) publish();
  spin_unlock(s_lock, irq);
  return true;
}

/* On Core 1 (a job): its alarm pool binds the interrupt to Core 1. */
static void start_timer_job(void *arg) {
  (void)arg;
  if (!s_pool) s_pool = alarm_pool_create_with_unused_hardware_alarm(2);
  alarm_pool_add_repeating_timer_us(s_pool, -MD_MUSIC_TIMER_US, timer_cb,
                                    NULL, &s_timer);
}

/* ------------------------------------------------------------------ */
/* Core 0                                                               */
/* ------------------------------------------------------------------ */

bool md_music_ok(void) { return s_ok; }

void md_music_init(uintptr_t rom_base) {
#if PICO_ON_DEVICE
  /* The windows are the USB controller's RAM, which reads 0 and ignores
   * writes while the controller is held in reset, as it is from boot when
   * nothing uses USB: out of reset, and never enabled, it leaves the RAM
   * to us. (EmuMD's is plain memory, so beta.3 played silence on a real
   * Multi-device and fine in the emulator.) */
  unreset_block_wait(RESETS_RESET_USBCTRL_BITS);
#endif
  /* That it holds a pattern, or the ST keeps the music (no MD_CAP_MUSIC). */
  {
    volatile uint32_t *w = (volatile uint32_t *)s_win;
    const unsigned words = MD_MUSIC_TRACKS * MD_MUSIC_WINDOW / 4u;
    s_ok = true;
    for (unsigned i = 0; i < words; i++) w[i] = 0xA55A0000u ^ (i * 0x9E3779B1u);
    for (unsigned i = 0; i < words; i++)
      if (w[i] != (0xA55A0000u ^ (i * 0x9E3779B1u))) s_ok = false;
    for (unsigned i = 0; i < words; i++) w[i] = 0;
    DPRINTF("music: window RAM %s\n", s_ok ? "ok" : "not working");
  }
  s_newest = (volatile uint16_t *)(rom_base + MD_YM_NEWEST_OFFSET);
  s_slots = (volatile uint16_t *)(rom_base + MD_YM_SLOT_OFFSET);
  *s_newest = 0;
  for (int i = 0; i < MD_YM_SLOTS * MD_YM_WORDS; i++) s_slots[i] = 0;
  s_lock = spin_lock_instance((uint)spin_lock_claim_unused(true));
  ymmusic_init();
  s_due = time_us_64() + MD_MUSIC_STEP_US;
  md_core1_dispatch(start_timer_job, NULL);
  md_core1_wait();
}

/* Fill track i's window from `from` (a WAD offset in the track). */
static bool fill(int i, uint32_t from) {
  uint32_t n = s_end[i] - from;
  if (n > MD_MUSIC_WINDOW) n = MD_MUSIC_WINDOW;
  if (n && !md_pack_wad_read(from, s_win[i], n)) return false;
  ymmusic_midi_track_t *t = ymmusic_md_track(i);
  t->start = t->ptr = s_win[i];
  t->end = s_win[i] + n;
  s_next[i] = from + n;
  return true;
}

static void stop(void) {
  const uint32_t irq = spin_lock_blocking(s_lock);
  ymmusic_md_stop();
  s_tracks = 0;
  s_rewind = REWIND_NONE;
  publish();
  spin_unlock(s_lock, irq);
}

/* The song's MIDI header and track chunks, read from the WAD; then a window
 * of each track, and the player started. */
static bool play(int lump, bool loop) {
  uint32_t pos, size;
  uint8_t h[14];

  stop();
  if (!md_pack_wad_lump(lump, &pos, &size) || size < 14 ||
      !md_pack_wad_read(pos, h, 14) || memcmp(h, "MThd", 4) != 0)
    return false;
  const uint32_t header = ((uint32_t)h[4] << 24) | ((uint32_t)h[5] << 16) |
                          ((uint32_t)h[6] << 8) | h[7];
  const unsigned count = ((unsigned)h[10] << 8) | h[11];
  const uint16_t division = (uint16_t)(((unsigned)h[12] << 8) | h[13]);
  uint32_t at = pos + 8 + header;
  const uint32_t end = pos + size;
  int tracks = 0;

  /* The player's tracks are touched only under the lock, and it is
   * stopped: the windows can be filled outside it. */
  for (unsigned k = 0; k < count && tracks < MD_MUSIC_TRACKS; k++) {
    uint8_t c[8];
    if (at + 8 > end || !md_pack_wad_read(at, c, 8) ||
        memcmp(c, "MTrk", 4) != 0)
      break;
    const uint32_t len = ((uint32_t)c[4] << 24) | ((uint32_t)c[5] << 16) |
                         ((uint32_t)c[6] << 8) | c[7];
    s_first[tracks] = at + 8;
    s_end[tracks] = (at + 8 + len > end) ? end : at + 8 + len;
    if (!fill(tracks, s_first[tracks])) return false;
    tracks++;
    at += 8 + len;
  }
  if (tracks == 0) return false;

  const uint32_t irq = spin_lock_blocking(s_lock);
  s_tracks = tracks;
  ymmusic_md_begin(division, tracks, loop);
  spin_unlock(s_lock, irq);
  DPRINTF("music: lump %d, %d tracks\n", lump, tracks);
  return true;
}

void md_music_command(unsigned action, int lump, bool loop, unsigned volume) {
  uint32_t irq;

  if (!s_ok) return; /* not offered (MD_CAP_MUSIC) */
  switch (action) {
    case MD_MUSIC_PLAY:
      irq = spin_lock_blocking(s_lock);
      ymmusic_master = (int)(volume > 255 ? 255 : volume);
      spin_unlock(s_lock, irq);
      if (!play(lump, loop)) stop();
      break;
    case MD_MUSIC_STOP:
      stop();
      break;
    case MD_MUSIC_PAUSE:
    case MD_MUSIC_CONTINUE:
      irq = spin_lock_blocking(s_lock);
      ymmusic_md_pause(action == MD_MUSIC_PAUSE);
      spin_unlock(s_lock, irq);
      break;
    case MD_MUSIC_VOLUME:
      irq = spin_lock_blocking(s_lock);
      ymmusic_master = (int)(volume > 255 ? 255 : volume);
      spin_unlock(s_lock, irq);
      break;
    default:
      break;
  }
}

/* Between commands: windows topped up from the WAD (read outside the lock,
 * then slid and appended under it), and a loop's rewind. */
void md_music_service(void) {
  if (s_tracks == 0) return;

  if (s_rewind == REWIND_WANTED) {
    bool ok = true;
    s_rewind = REWIND_FILLING;
    /* The player touches no track until REWIND_DONE: no lock needed. */
    for (int i = 0; i < s_tracks && ok; i++) ok = fill(i, s_first[i]);
    __dmb();
    if (ok)
      s_rewind = REWIND_DONE;
    else
      stop();
    return;
  }
  if (s_rewind != REWIND_NONE) return;

  for (int i = 0; i < s_tracks; i++) {
    ymmusic_midi_track_t *t = ymmusic_md_track(i);
    if (s_next[i] >= s_end[i] || !t->active) continue;
    if ((t->end - t->ptr) >= MD_MUSIC_TOPUP) continue;
    /* What fits after what is left; the player only takes bytes away
     * meanwhile, so it still fits once under the lock. */
    uint32_t n = MD_MUSIC_WINDOW - (uint32_t)(t->end - t->ptr);
    if (n > s_end[i] - s_next[i]) n = s_end[i] - s_next[i];
    if (!md_pack_wad_read(s_next[i], s_read, n)) {
      stop();
      return;
    }
    const uint32_t irq = spin_lock_blocking(s_lock);
    const uint32_t left = (uint32_t)(t->end - t->ptr);
    memmove(s_win[i], t->ptr, left);
    memcpy(s_win[i] + left, s_read, n);
    t->ptr = t->start = s_win[i];
    t->end = s_win[i] + left + n;
    s_next[i] += n;
    spin_unlock(s_lock, irq);
  }
}
