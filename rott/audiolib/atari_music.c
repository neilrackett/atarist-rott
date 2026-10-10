/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stddef.h>
#include <stdbool.h>
#if defined(YMMUSIC_MD)
/* Built into the ROTT Accelerator's firmware too (sidecart/rp/src/
 * md_music.c): the same player, its YM writes kept in ymmusic_regs for the
 * ST to copy, its MIDI tracks streamed through small windows
 * (YMMUSIC_TRACK_READY, ymmusic_md_rewind). Only MIDI songs come that way. */
#define SHORT(x) (x)
#else
#include <stdio.h>
#include "i_swap.h"
#endif
#include "atari_music.h"

// Music volume, 0-255 (MUSIC_SetVolume).
int ymmusic_master = 196;

// A linear amplitude 0-127 as a YM2149 level 0-15. The YM's levels are
// about 3dB apart, so 15 + 20 log10(a / 127) / 3, rounded: subtracting
// from the level instead (as this did) put the music 24dB down at the
// default volume, and a quiet note at nothing at all.
static const unsigned char ymmusic_levels[128] = {
    0, 1, 3, 4, 5, 6, 6, 7, 7, 7, 8, 8, 8, 8, 9, 9,
    9, 9, 9, 9, 10, 10, 10, 10, 10, 10, 10, 11, 11, 11, 11, 11,
    11, 11, 11, 11, 11, 11, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
    12, 12, 12, 12, 12, 12, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
    13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 14, 14, 14, 14,
    14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
    14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15
};

typedef struct
{
    // Points to an array of data points (signed: char is unsigned on ARM,
    // where the ROTT Accelerator runs this too).
    signed char *data;
    // Indexes into array which mark certain points in time.
    unsigned char sustain_begin, release_begin, last;
} envelope_t;

typedef struct
{
    const char *name;
    envelope_t *volume_envelope;
    envelope_t *pitch_envelope;
    envelope_t *note_envelope;
    unsigned char override_note;
    unsigned char overrides_note : 1;
    unsigned char enables_noise : 1;
} instrument_t;

// Contains all data needed to drive a single YM-2149 hardware channel
typedef struct
{
    // Number of ticks since note was pressed or released (0xffff means off).
    unsigned short ticks;
    // MUS Channel
    unsigned char channel;
    // MUS Note 0..127
    unsigned char note;
    // Index of YM channel
    unsigned char ymidx;
    // Whether the note has been released
    unsigned char released;
    // MIDI note velocity 0..127 (127 for MUS, whose notes set the channel's volume)
    unsigned char velocity;
    // Instrument playing
    instrument_t *instrument;
} ymmusic_voice_t;

typedef struct
{
    // MUS instrument index assigned to channel
    unsigned char instrument;
    // MUS Volume 0..127
    unsigned char volume;
    // MUS Pitch bend (0..255)
    unsigned char pitch_bend;
} ymmusic_channel_t;

#define ENVELOPE(dataname, sustain, release) {          \
    .data = dataname,                                   \
    .sustain_begin = sustain,                           \
    .release_begin = release,                           \
    .last = sizeof(dataname)-1}

static signed char overdriven_guitar_volume_envelope_data[] =
    {-70, -40, -10, 0, -1, -1, -2, -2, -3, -3, -4, -4, -5, -5, -6, -6, -7, -7, -8, -8, -9, -9, -10, -10};
static envelope_t overdriven_guitar_volume_envelope = ENVELOPE(overdriven_guitar_volume_envelope_data, 3, 5);
static instrument_t overdriven_guitar = {
    .name = "Overdriven Guitar",
    .volume_envelope = &overdriven_guitar_volume_envelope,
};

static signed char distortion_guitar_volume_envelope_data[] =
    {-20, 60, 10, 0, 8, -1, -1, -2, -3, -3, -4, -4, -5, -5, -6, -6, -7, -7, -8, -8, -9, -9, -10, -10};
static signed char distortion_guitar_pitch_envelope_data[] =
    {80, 40, 20, 0, 20, 0};
static envelope_t distortion_guitar_volume_envelope = ENVELOPE(distortion_guitar_volume_envelope_data, 3, 5);
static envelope_t distortion_guitar_pitch_envelope = ENVELOPE(distortion_guitar_pitch_envelope_data, 3, 5);
static instrument_t distortion_guitar = {
    .name = "Distortion Guitar",
    .volume_envelope = &distortion_guitar_volume_envelope,
    .pitch_envelope = &distortion_guitar_pitch_envelope,
};

static signed char dummy_instrument_volume_envelope_data[] =
    {-20, 0, -16, -32, -64};
static envelope_t dummy_instrument_volume_envelope = ENVELOPE(dummy_instrument_volume_envelope_data, 1, 2);
static instrument_t dummy_instrument = {
    .name = "Dummy Instrument",
    .volume_envelope = &dummy_instrument_volume_envelope,
};

static signed char bass_drum_volume_envelope_data[] =
    {0, 0, 0, 0, 0, -60, -80, -100, -120};
static signed char bass_drum_note_envelope_data[] =
    {0, -4, -8, -18, -26, -32, -35, -35, -36};
static envelope_t bass_drum_volume_envelope = ENVELOPE(bass_drum_volume_envelope_data, 16, 16);
static envelope_t bass_drum_note_envelope = ENVELOPE(bass_drum_note_envelope_data, 16, 16);
static instrument_t bass_drum = {
    .name = "Bass Drum",
    .volume_envelope = &bass_drum_volume_envelope,
    .note_envelope = &bass_drum_note_envelope,
    .override_note = 64,
    .overrides_note = 1,
};

static signed char snare_volume_envelope_data[] =
    {120, 20, 10, 4, 0, -4, -8, -12, -16, -20, -24, -28, -32, -34, -36, -38, -40, -41, -42, -43, -44, -45, -46, -47,
     -48, -49, -50, -51, -52, -53, -54, -55, -56, -57, -58, -59, -60, -61, -62, -63};
static envelope_t snare_volume_envelope = ENVELOPE(snare_volume_envelope_data, 127, 127);
static signed char electric_snare_note_envelope_data[] =
    {+48, +0, -16, -10, -30, -28, -37, -33, -36};
static envelope_t electric_snare_note_envelope = ENVELOPE(electric_snare_note_envelope_data, 127, 127);
static instrument_t electric_snare = {
    .name = "Electric Snare",
    .volume_envelope = &snare_volume_envelope,
    .note_envelope = &electric_snare_note_envelope,
    .override_note = 81,
    .overrides_note = 1,
    .enables_noise = 1,
};

static signed char dummy_percussion_volume_envelope_data[] =
    {40, -60, -98, -120};
static envelope_t dummy_percussion_volume_envelope = ENVELOPE(dummy_percussion_volume_envelope_data, 127, 127);
static instrument_t dummy_percussion = {
    .name = "Dummy Percussion",
    .volume_envelope = &dummy_percussion_volume_envelope,
    .override_note = 50,
    .overrides_note = 1,
    .enables_noise = 1,
};

#define YMMUSIC_READ_DELAY 16

#define YMMUSIC_PER_BYTE_PENALTY 0

// Set by gameloop when a music change is requested.
unsigned char *ymmusic_data_cmd = NULL;
unsigned short ymmusic_state_cmd = 0;

// Incremented by gameloop before making any changes (to ensure atomicity).
unsigned short ymmusic_cmd_nr_begin = 0;
// Incremented by gameloop after making any changes (to ensure atomicity).
unsigned short ymmusic_cmd_nr_end = 0;

// Set by interrupt on handling a request.
static unsigned char *ymmusic_data = NULL;
static unsigned char *ymmusic_ptr = NULL;
static unsigned char *ymmusic_end = NULL;
unsigned short ymmusic_state = 0;
static unsigned short ymmusic_wait = 0;
static unsigned short ymmusic_wait_remainder = 0;
static ymmusic_voice_t ymmusic_voices[3];
static ymmusic_channel_t ymmusic_channels[16];
static int ymmusic_mode = 0; /* 0=none, 1=MUS, 2=MIDI */

#define YMMUSIC_NUMVOICES (sizeof(ymmusic_voices) / sizeof(ymmusic_voice_t))
#define YMMUSIC_NUMCHANNELS (sizeof(ymmusic_channels) / sizeof(ymmusic_channel_t))

#define YMMUSIC_MIDI_ENDED 0xFFFFFFFFUL

static ymmusic_midi_track_t ymmusic_midi_tracks[YMMUSIC_MAX_MIDI_TRACKS];
static int ymmusic_midi_num_tracks = 0;
static unsigned short ymmusic_midi_division = 96;
static unsigned long ymmusic_midi_tempo_us = 500000; /* us per quarter note */
static unsigned long ymmusic_midi_us_per_tick = 500000 / 96;
static unsigned long ymmusic_midi_us_accum = 0;
static unsigned long ymmusic_midi_now = 0;  /* song ticks so far */
static unsigned long ymmusic_midi_next = 0; /* the earliest track's next event */
static int ymmusic_midi_active = 0;         /* tracks still going */

// Incremented by interrupt if any request is processed.
static unsigned short ymmusic_ack_nr = 0;

// YM2149 sound chip access, in supervisor mode only (the VBL, or Supexec).
// Interrupts are masked between selecting a register and using it: TOS's
// Timer C plays Dosound sequences on the same chip and outranks the VBL,
// so landing in between would send the value to the register it selected.
// (As STDL's ym.c does: https://github.com/neilrackett/atarist-stdl)
#if defined(YMMUSIC_MD)
unsigned char ymmusic_regs[14] = {0, 0, 0, 0, 0, 0, 0, 0x3f};

static __inline__ unsigned long ym_mulu(unsigned short a, unsigned short b)
{
    return (unsigned long)a * b;
}
#else
#define YM_SELECT (*(volatile unsigned char *)0xFFFF8800UL)
#define YM_DATA (*(volatile unsigned char *)0xFFFF8802UL)

// 16 x 16 bits in one mulu.w: an int multiply is a __mulsi3 call on a 68000.
static __inline__ unsigned long ym_mulu(unsigned short a, unsigned short b)
{
    unsigned long r = a;

    __asm__("mulu.w %1,%0" : "+d"(r) : "d"(b));
    return r;
}
#endif

// What was last written to each voice's period and volume, so a step only
// writes the registers that change. All ones: unknown, write next time.
static unsigned short ym_period_shadow[3] = {0xffff, 0xffff, 0xffff};
static unsigned char ym_volume_shadow[3] = {0xff, 0xff, 0xff};

#if defined(YMMUSIC_MD)
static void ym_write(unsigned char reg, unsigned char value)
{
    ymmusic_regs[reg] = value;
}

// The mixer's tone and noise bits; the ST keeps its port bits as they are.
static void ym_mixer(unsigned char clear, unsigned char set)
{
    ymmusic_regs[7] = (unsigned char)(((ymmusic_regs[7] & ~clear) | set) & 0x3f);
}
#else
static void ym_write(unsigned char reg, unsigned char value)
{
    unsigned short sr;

    __asm__ volatile("move.w %%sr,%0\n\tori.w #0x0700,%%sr" : "=d"(sr) : : "cc");
    YM_SELECT = reg;
    YM_DATA = value;
    __asm__ volatile("move.w %0,%%sr" : : "d"(sr) : "cc");
}

// The mixer (register 7): set and clear tone/noise bits, keeping the I/O
// port direction bits TOS relies on (port A drives the floppy select).
static void ym_mixer(unsigned char clear, unsigned char set)
{
    unsigned short sr;

    __asm__ volatile("move.w %%sr,%0\n\tori.w #0x0700,%%sr" : "=d"(sr) : : "cc");
    YM_SELECT = 7;
    YM_DATA = (unsigned char)((YM_SELECT & ~clear) | set);
    __asm__ volatile("move.w %0,%%sr" : : "d"(sr) : "cc");
}
#endif

// Divisor table for MUS notes * 4 bit precision for pitch bend
// [note 0..127][pitch bend 0..15]
// This data was generated using the 'ym_table.c' tool
static const short ymmusic_divisors[128][16] = {
  { 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095},
  { 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095},
  { 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095},
  { 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095},
  { 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095},
  { 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095},
  { 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095},
  { 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095},
  { 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095},
  { 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095},
  { 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095},
  { 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095},
  { 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095},
  { 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095},
  { 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095},
  { 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095},
  { 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095},
  { 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095},
  { 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095},
  { 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095},
  { 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095},
  { 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095},
  { 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095},
  { 4050, 4065, 4079, 4094, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095, 4095},
  { 3823, 3837, 3850, 3864, 3878, 3892, 3906, 3921, 3935, 3949, 3963, 3978, 3992, 4006, 4021, 4035},
  { 3608, 3621, 3634, 3648, 3661, 3674, 3687, 3701, 3714, 3727, 3741, 3754, 3768, 3782, 3795, 3809},
  { 3406, 3418, 3430, 3443, 3455, 3468, 3480, 3493, 3506, 3518, 3531, 3544, 3557, 3569, 3582, 3595},
  { 3215, 3226, 3238, 3250, 3261, 3273, 3285, 3297, 3309, 3321, 3333, 3345, 3357, 3369, 3381, 3393},
  { 3034, 3045, 3056, 3067, 3078, 3089, 3101, 3112, 3123, 3134, 3146, 3157, 3169, 3180, 3191, 3203},
  { 2864, 2874, 2885, 2895, 2906, 2916, 2927, 2937, 2948, 2959, 2969, 2980, 2991, 3002, 3012, 3023},
  { 2703, 2713, 2723, 2733, 2743, 2752, 2762, 2772, 2782, 2793, 2803, 2813, 2823, 2833, 2843, 2854},
  { 2552, 2561, 2570, 2579, 2589, 2598, 2607, 2617, 2626, 2636, 2645, 2655, 2664, 2674, 2684, 2694},
  { 2408, 2417, 2426, 2435, 2443, 2452, 2461, 2470, 2479, 2488, 2497, 2506, 2515, 2524, 2533, 2542},
  { 2273, 2281, 2290, 2298, 2306, 2315, 2323, 2331, 2340, 2348, 2357, 2365, 2374, 2382, 2391, 2400},
  { 2146, 2153, 2161, 2169, 2177, 2185, 2193, 2201, 2209, 2217, 2225, 2233, 2241, 2249, 2257, 2265},
  { 2025, 2033, 2040, 2047, 2055, 2062, 2070, 2077, 2085, 2092, 2100, 2107, 2115, 2123, 2130, 2138},
  { 1912, 1919, 1925, 1932, 1939, 1946, 1953, 1961, 1968, 1975, 1982, 1989, 1996, 2003, 2011, 2018},
  { 1804, 1811, 1817, 1824, 1831, 1837, 1844, 1851, 1857, 1864, 1871, 1877, 1884, 1891, 1898, 1905},
  { 1703, 1709, 1715, 1722, 1728, 1734, 1740, 1747, 1753, 1759, 1766, 1772, 1779, 1785, 1791, 1798},
  { 1608, 1613, 1619, 1625, 1631, 1637, 1643, 1649, 1655, 1661, 1667, 1673, 1679, 1685, 1691, 1697},
  { 1517, 1523, 1528, 1534, 1539, 1545, 1551, 1556, 1562, 1567, 1573, 1579, 1585, 1590, 1596, 1602},
  { 1432, 1437, 1443, 1448, 1453, 1458, 1464, 1469, 1474, 1480, 1485, 1490, 1496, 1501, 1506, 1512},
  { 1352, 1357, 1362, 1367, 1372, 1376, 1381, 1386, 1391, 1397, 1402, 1407, 1412, 1417, 1422, 1427},
  { 1276, 1281, 1285, 1290, 1295, 1299, 1304, 1309, 1313, 1318, 1323, 1328, 1332, 1337, 1342, 1347},
  { 1204, 1209, 1213, 1218, 1222, 1226, 1231, 1235, 1240, 1244, 1249, 1253, 1258, 1262, 1267, 1271},
  { 1137, 1141, 1145, 1149, 1153, 1158, 1162, 1166, 1170, 1174, 1179, 1183, 1187, 1191, 1196, 1200},
  { 1073, 1077, 1081, 1085, 1089, 1093, 1097, 1101, 1105, 1109, 1113, 1117, 1121, 1125, 1129, 1133},
  { 1013, 1017, 1020, 1024, 1028, 1031, 1035, 1039, 1043, 1046, 1050, 1054, 1058, 1062, 1065, 1069},
  { 956, 960, 963, 966, 970, 973, 977, 981, 984, 988, 991, 995, 998, 1002, 1006, 1009},
  { 902, 906, 909, 912, 916, 919, 922, 926, 929, 932, 936, 939, 942, 946, 949, 953},
  { 852, 855, 858, 861, 864, 867, 870, 874, 877, 880, 883, 886, 890, 893, 896, 899},
  { 804, 807, 810, 813, 816, 819, 822, 825, 828, 831, 834, 837, 840, 843, 846, 849},
  { 759, 762, 764, 767, 770, 773, 776, 778, 781, 784, 787, 790, 793, 795, 798, 801},
  { 716, 719, 722, 724, 727, 729, 732, 735, 737, 740, 743, 745, 748, 751, 753, 756},
  { 676, 679, 681, 684, 686, 688, 691, 693, 696, 699, 701, 704, 706, 709, 711, 714},
  { 638, 641, 643, 645, 648, 650, 652, 655, 657, 659, 662, 664, 666, 669, 671, 674},
  { 602, 605, 607, 609, 611, 613, 616, 618, 620, 622, 625, 627, 629, 631, 634, 636},
  { 569, 571, 573, 575, 577, 579, 581, 583, 585, 587, 590, 592, 594, 596, 598, 600},
  { 537, 539, 541, 543, 545, 547, 549, 551, 553, 555, 557, 559, 561, 563, 565, 567},
  { 507, 509, 510, 512, 514, 516, 518, 520, 522, 523, 525, 527, 529, 531, 533, 535},
  { 478, 480, 482, 483, 485, 487, 489, 491, 492, 494, 496, 498, 499, 501, 503, 505},
  { 451, 453, 455, 456, 458, 460, 461, 463, 465, 466, 468, 470, 471, 473, 475, 477},
  { 426, 428, 429, 431, 432, 434, 435, 437, 439, 440, 442, 443, 445, 447, 448, 450},
  { 402, 404, 405, 407, 408, 410, 411, 413, 414, 416, 417, 419, 420, 422, 423, 425},
  { 380, 381, 382, 384, 385, 387, 388, 389, 391, 392, 394, 395, 397, 398, 399, 401},
  { 358, 360, 361, 362, 364, 365, 366, 368, 369, 370, 372, 373, 374, 376, 377, 378},
  { 338, 340, 341, 342, 343, 344, 346, 347, 348, 350, 351, 352, 353, 355, 356, 357},
  { 319, 321, 322, 323, 324, 325, 326, 328, 329, 330, 331, 332, 333, 335, 336, 337},
  { 301, 303, 304, 305, 306, 307, 308, 309, 310, 311, 313, 314, 315, 316, 317, 318},
  { 285, 286, 287, 288, 289, 290, 291, 292, 293, 294, 295, 296, 297, 298, 299, 300},
  { 269, 270, 271, 272, 273, 274, 275, 276, 277, 278, 279, 280, 281, 282, 283, 284},
  { 254, 255, 255, 256, 257, 258, 259, 260, 261, 262, 263, 264, 265, 266, 267, 268},
  { 239, 240, 241, 242, 243, 244, 245, 246, 246, 247, 248, 249, 250, 251, 252, 253},
  { 226, 227, 228, 228, 229, 230, 231, 232, 233, 233, 234, 235, 236, 237, 238, 239},
  { 213, 214, 215, 216, 216, 217, 218, 219, 220, 220, 221, 222, 223, 224, 224, 225},
  { 201, 202, 203, 204, 204, 205, 206, 207, 207, 208, 209, 210, 210, 211, 212, 213},
  { 190, 191, 191, 192, 193, 194, 194, 195, 196, 196, 197, 198, 199, 199, 200, 201},
  { 179, 180, 181, 181, 182, 183, 183, 184, 185, 185, 186, 187, 187, 188, 189, 189},
  { 169, 170, 171, 171, 172, 172, 173, 174, 174, 175, 176, 176, 177, 178, 178, 179},
  { 160, 161, 161, 162, 162, 163, 163, 164, 165, 165, 166, 166, 167, 168, 168, 169},
  { 151, 152, 152, 153, 153, 154, 154, 155, 155, 156, 157, 157, 158, 158, 159, 159},
  { 143, 143, 144, 144, 145, 145, 146, 146, 147, 147, 148, 148, 149, 149, 150, 150},
  { 135, 135, 136, 136, 137, 137, 138, 138, 139, 139, 140, 140, 141, 141, 142, 142},
  { 127, 128, 128, 128, 129, 129, 130, 130, 131, 131, 132, 132, 133, 133, 134, 134},
  { 120, 120, 121, 121, 122, 122, 123, 123, 123, 124, 124, 125, 125, 126, 126, 127},
  { 113, 114, 114, 114, 115, 115, 116, 116, 117, 117, 117, 118, 118, 119, 119, 120},
  { 107, 107, 108, 108, 108, 109, 109, 110, 110, 110, 111, 111, 112, 112, 112, 113},
  { 101, 101, 102, 102, 102, 103, 103, 104, 104, 104, 105, 105, 105, 106, 106, 107},
  { 95, 96, 96, 96, 97, 97, 97, 98, 98, 98, 99, 99, 100, 100, 100, 101},
  { 90, 90, 91, 91, 91, 92, 92, 92, 93, 93, 93, 94, 94, 94, 95, 95},
  { 85, 85, 86, 86, 86, 86, 87, 87, 87, 88, 88, 88, 89, 89, 89, 90},
  { 80, 81, 81, 81, 81, 82, 82, 82, 83, 83, 83, 83, 84, 84, 84, 85},
  { 76, 76, 76, 77, 77, 77, 77, 78, 78, 78, 79, 79, 79, 79, 80, 80},
  { 72, 72, 72, 72, 73, 73, 73, 73, 74, 74, 74, 74, 75, 75, 75, 75},
  { 68, 68, 68, 68, 69, 69, 69, 69, 70, 70, 70, 70, 71, 71, 71, 71},
  { 64, 64, 64, 64, 65, 65, 65, 65, 66, 66, 66, 66, 67, 67, 67, 67},
  { 60, 60, 61, 61, 61, 61, 62, 62, 62, 62, 62, 63, 63, 63, 63, 64},
  { 57, 57, 57, 57, 58, 58, 58, 58, 59, 59, 59, 59, 59, 60, 60, 60},
  { 54, 54, 54, 54, 54, 55, 55, 55, 55, 55, 56, 56, 56, 56, 56, 57},
  { 51, 51, 51, 51, 51, 52, 52, 52, 52, 52, 53, 53, 53, 53, 53, 54},
  { 48, 48, 48, 48, 49, 49, 49, 49, 49, 49, 50, 50, 50, 50, 50, 51},
  { 45, 45, 46, 46, 46, 46, 46, 46, 47, 47, 47, 47, 47, 47, 48, 48},
  { 43, 43, 43, 43, 43, 43, 44, 44, 44, 44, 44, 44, 45, 45, 45, 45},
  { 40, 41, 41, 41, 41, 41, 41, 41, 42, 42, 42, 42, 42, 42, 42, 43},
  { 38, 38, 38, 39, 39, 39, 39, 39, 39, 39, 40, 40, 40, 40, 40, 40},
  { 36, 36, 36, 36, 37, 37, 37, 37, 37, 37, 37, 37, 38, 38, 38, 38},
  { 34, 34, 34, 34, 35, 35, 35, 35, 35, 35, 35, 35, 36, 36, 36, 36},
  { 32, 32, 32, 32, 33, 33, 33, 33, 33, 33, 33, 33, 34, 34, 34, 34},
  { 30, 30, 31, 31, 31, 31, 31, 31, 31, 31, 31, 32, 32, 32, 32, 32},
  { 29, 29, 29, 29, 29, 29, 29, 29, 30, 30, 30, 30, 30, 30, 30, 30},
  { 27, 27, 27, 27, 27, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 29},
  { 26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 27, 27, 27, 27, 27, 27},
  { 24, 24, 24, 24, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 25, 26},
  { 23, 23, 23, 23, 23, 23, 23, 23, 24, 24, 24, 24, 24, 24, 24, 24},
  { 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 23, 23, 23, 23},
  { 20, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 22},
  { 19, 19, 19, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20},
  { 18, 18, 18, 18, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19},
  { 17, 17, 17, 17, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18},
  { 16, 16, 16, 16, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17},
  { 15, 15, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16},
  { 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15},
  { 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 15},
  { 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 14, 14, 14, 14, 14, 14},
  { 12, 12, 12, 12, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13},
  { 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12},
  { 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 12, 12, 12, 12},
  { 10, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11},
};

static void ymmusic_play_note(unsigned char channel, unsigned char note, unsigned char use_volume, unsigned char volume);
static void ymmusic_release_note(unsigned char channel, unsigned char note);
static void ymmusic_pitch_bend(unsigned char channel, unsigned char pitch_bend);
static void ymmusic_controller(unsigned char channel, unsigned char control, unsigned char value);

static unsigned short ymmusic_be16(const unsigned char *p)
{
    return (unsigned short)(((unsigned short)p[0] << 8) | p[1]);
}

static unsigned long ymmusic_be32(const unsigned char *p)
{
    return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) |
           ((unsigned long)p[2] << 8) | (unsigned long)p[3];
}

static unsigned long ymmusic_read_var(unsigned char **pp, unsigned char *end)
{
    unsigned long value = 0;
    int guard = 0;
    while ((*pp < end) && (guard++ < 4))
    {
        unsigned char c = *(*pp)++;
        value = (value << 7) | (unsigned long)(c & 0x7f);
        if (!(c & 0x80))
            break;
    }
    return value;
}

static void ymmusic_midi_reset_tracks(void)
{
    int i;

    ymmusic_midi_active = 0;    for (i = 0; i < ymmusic_midi_num_tracks; ++i)
    {
        ymmusic_midi_track_t *t = &ymmusic_midi_tracks[i];
        t->ptr = t->start;
        t->running_status = 0;
        t->active = (t->start < t->end) ? 1 : 0;
        t->next = t->active ? ymmusic_read_var(&t->ptr, t->end) : YMMUSIC_MIDI_ENDED;
        ymmusic_midi_active += t->active;
    }
    ymmusic_midi_tempo_us = 500000;
    ymmusic_midi_us_per_tick = ymmusic_midi_tempo_us / ymmusic_midi_division;
    ymmusic_midi_us_accum = 0;
    ymmusic_midi_now = 0;
    ymmusic_midi_next = 0; /* the first step finds it */
}

static int ymmusic_midi_init(unsigned char *data)
{
    unsigned char *p;
    unsigned long header_len;
    unsigned short tracks;
    int parsed = 0;
    int i;

    if (!data)
        return 0;
    if (!(data[0] == 'M' && data[1] == 'T' && data[2] == 'h' && data[3] == 'd'))
        return 0;

    header_len = ymmusic_be32(data + 4);
    if (header_len < 6)
        return 0;

    tracks = ymmusic_be16(data + 10);
    ymmusic_midi_division = ymmusic_be16(data + 12);
    if (ymmusic_midi_division == 0)
        ymmusic_midi_division = 96;

    p = data + 8 + header_len;
    ymmusic_midi_num_tracks = 0;

    for (i = 0; i < tracks && ymmusic_midi_num_tracks < YMMUSIC_MAX_MIDI_TRACKS; ++i)
    {
        unsigned long len;
        unsigned char *track_data;
        unsigned char *track_end;

        if (!(p[0] == 'M' && p[1] == 'T' && p[2] == 'r' && p[3] == 'k'))
            break;

        len = ymmusic_be32(p + 4);
        track_data = p + 8;
        track_end = track_data + len;

        ymmusic_midi_tracks[ymmusic_midi_num_tracks].start = track_data;
        ymmusic_midi_tracks[ymmusic_midi_num_tracks].end = track_end;
        ++ymmusic_midi_num_tracks;
        ++parsed;

        p = track_end;
    }

    if (parsed <= 0)
        return 0;

    ymmusic_midi_reset_tracks();
    return 1;
}

// A track has run out: off, and one fewer going.
static void ymmusic_midi_end_track(ymmusic_midi_track_t *t)
{
    if (t->active)
    {
        t->active = 0;
        t->next = YMMUSIC_MIDI_ENDED;
        --ymmusic_midi_active;
    }
}

// MIDI keeps its drums on channel 10 (9 from 0); the player came from MUS,
// which keeps them on 15, and picks percussion instruments there. Swap the
// two so the drums sound as drums.
static unsigned char ymmusic_midi_channel(unsigned char status)
{
    unsigned char channel = status & 0x0F;

    return (channel == 9) ? 15 : (channel == 15) ? 9 : channel;
}

static void ymmusic_midi_process_track_event(ymmusic_midi_track_t *t)
{
    unsigned char status;
    unsigned char b;

    if (!t->active || t->ptr >= t->end)
    {
        ymmusic_midi_end_track(t);
        return;
    }

    b = *t->ptr++;
    if (b < 0x80)
    {
        if (t->running_status == 0)
        {
            ymmusic_midi_end_track(t);
            return;
        }
        status = t->running_status;
        --t->ptr;
    }
    else
    {
        status = b;
        if (status < 0xF0)
            t->running_status = status;
    }

    switch (status & 0xF0)
    {
    case 0x80:
    {
        unsigned char channel = ymmusic_midi_channel(status);
        unsigned char note = (t->ptr < t->end) ? *t->ptr++ : 0;
        if (t->ptr < t->end)
            ++t->ptr; /* velocity */
        ymmusic_release_note(channel, note & 0x7f);
        break;
    }
    case 0x90:
    {
        unsigned char channel = ymmusic_midi_channel(status);
        unsigned char note = (t->ptr < t->end) ? *t->ptr++ : 0;
        unsigned char vel = (t->ptr < t->end) ? *t->ptr++ : 0;
        if (vel == 0)
            ymmusic_release_note(channel, note & 0x7f);
        else
            ymmusic_play_note(channel, note & 0x7f, 1, vel);
        break;
    }
    case 0xA0:
    case 0xB0:
    {
        unsigned char channel = ymmusic_midi_channel(status);
        unsigned char c1 = (t->ptr < t->end) ? *t->ptr++ : 0;
        unsigned char c2 = (t->ptr < t->end) ? *t->ptr++ : 0;
        if ((status & 0xF0) == 0xB0)
        {
            unsigned char mus_control = c1;
            if (c1 == 7)
                mus_control = 3; /* MIDI channel volume -> MUS volume */
            else if (c1 == 10)
                mus_control = 4; /* MIDI pan -> MUS pan */
            ymmusic_controller(channel, mus_control, c2);
        }
        break;
    }
    case 0xC0:
    {
        unsigned char channel = ymmusic_midi_channel(status);
        unsigned char patch = (t->ptr < t->end) ? *t->ptr++ : 0;
        ymmusic_controller(channel, 0, patch);
        break;
    }
    case 0xD0:
        if (t->ptr < t->end)
            ++t->ptr;
        break;
    case 0xE0:
    {
        unsigned char channel = ymmusic_midi_channel(status);
        unsigned char lsb = (t->ptr < t->end) ? *t->ptr++ : 0;
        unsigned char msb = (t->ptr < t->end) ? *t->ptr++ : 0;
        unsigned short bend = (unsigned short)(((unsigned short)msb << 7) | (lsb & 0x7F));
        ymmusic_pitch_bend(channel, (unsigned char)(bend >> 6));
        break;
    }
    default:
        if (status == 0xFF)
        {
            unsigned char type = (t->ptr < t->end) ? *t->ptr++ : 0;
            unsigned long len = ymmusic_read_var(&t->ptr, t->end);
            unsigned char *meta_end = t->ptr + len;
            if (meta_end > t->end)
                meta_end = t->end;

            if (type == 0x2F)
            {
                ymmusic_midi_end_track(t);
                t->ptr = t->end;
                return;
            }
            if (type == 0x51 && len >= 3)
            {
                ymmusic_midi_tempo_us = ((unsigned long)t->ptr[0] << 16) |
                                        ((unsigned long)t->ptr[1] << 8) |
                                        (unsigned long)t->ptr[2];
                if (ymmusic_midi_tempo_us == 0)
                    ymmusic_midi_tempo_us = 500000;
                // the division happens here, not on every step
                ymmusic_midi_us_per_tick = ymmusic_midi_tempo_us / ymmusic_midi_division;
                if (ymmusic_midi_us_per_tick == 0)
                    ymmusic_midi_us_per_tick = 1;
            }

            t->ptr = meta_end;
        }
        else if (status == 0xF0 || status == 0xF7)
        {
            unsigned long len = ymmusic_read_var(&t->ptr, t->end);
            unsigned char *syx_end = t->ptr + len;
            if (syx_end > t->end)
                syx_end = t->end;
            t->ptr = syx_end;
        }
        break;
    }

    if (t->ptr >= t->end)
    {
        ymmusic_midi_end_track(t);
        return;
    }

    t->next += ymmusic_read_var(&t->ptr, t->end);
}

// One 50Hz step: move the song clock on by 20ms worth of MIDI ticks, then
// play every event now due, earliest first across the tracks. This used to
// step tick by tick, counting each track's delta down every tick, which on
// a song of a dozen tracks took 27,000 cycles a step: 8% of a 16MHz Mega
// STE. Now a step with nothing due is one comparison a track.
static void ymmusic_midi_update(void)
{
    if (ymmusic_midi_active == 0)
    {
        if (ymmusic_state & YMMUSIC_LOOP)
        {
#if defined(YMMUSIC_MD)
            if (!ymmusic_md_rewind())
                return; /* the windows go back to the tracks' starts first */
#endif
            ymmusic_midi_reset_tracks();
        }
        else
        {
            ymmusic_state &= ~YMMUSIC_PLAY;
            return;
        }
    }

    ymmusic_midi_us_accum += 20000; /* 50 Hz service cadence */
    while (ymmusic_midi_us_accum >= ymmusic_midi_us_per_tick)
    {
        ymmusic_midi_us_accum -= ymmusic_midi_us_per_tick;
        ++ymmusic_midi_now;
    }

    // Nothing due yet: the usual step, and no scan of the tracks at all.
    // Otherwise play the earliest track's event, find the earliest again,
    // and so on until the earliest is in the future.
    while (ymmusic_midi_active && ymmusic_midi_next <= ymmusic_midi_now)
    {
        ymmusic_midi_track_t *t = ymmusic_midi_tracks;
        ymmusic_midi_track_t *end = t + ymmusic_midi_num_tracks;
        ymmusic_midi_track_t *due = NULL;
        unsigned long earliest = YMMUSIC_MIDI_ENDED;
        unsigned long second = YMMUSIC_MIDI_ENDED;

        // The earliest track (the first, on a tie) and the time after it.
        // One long a track: an ended track's is all ones, never earliest.
        for (; t < end; ++t)
        {
            unsigned long next = t->next;

            if (next < earliest)
            {
                second = earliest;
                earliest = next;
                due = t;
            }
            else if (next < second)
            {
                second = next;
            }
        }
        if (due == NULL)
            break;
        ymmusic_midi_next = earliest;
        if (earliest > ymmusic_midi_now || !YMMUSIC_TRACK_READY(due))
            break;
        // Its events, while none of another track's comes first (on a tie
        // the scan decides, as the first track's goes first)
        do
        {
            ymmusic_midi_process_track_event(due);
        } while (due->next <= ymmusic_midi_now && due->next < second &&
                 YMMUSIC_TRACK_READY(due));
        // The earliest now, with no scan (`second` is the earliest of the
        // others): one scan less a step, as the last one only found this.
        ymmusic_midi_next = (due->next < second) ? due->next : second;
    }
}

static void ymmusic_reset()
{
    int i;

    // Initialize mixer: disable all noise and tone
    ym_mixer(0, 0x3f);
    for (i = 0; i < 3; i++)
    {
        ym_period_shadow[i] = 0xffff;
        ym_volume_shadow[i] = 0xff;
    }

    ymmusic_ptr = NULL;
    ymmusic_end = NULL;
    ymmusic_mode = 0;
    ymmusic_wait = 0;
    ymmusic_wait_remainder = 0;
    ymmusic_midi_num_tracks = 0;
    ymmusic_midi_us_accum = 0;
    ymmusic_midi_now = 0;
    ymmusic_midi_active = 0;

    for (i = 0; i < YMMUSIC_NUMVOICES; i++)
    {
        ymmusic_voices[i].ticks = 0xffff;
        ymmusic_voices[i].channel = 0xff;
        ymmusic_voices[i].ymidx = i;
    }
    for (i = 0; i < YMMUSIC_NUMCHANNELS; i++)
    {
        ymmusic_channels[i].instrument = 0;
        ymmusic_channels[i].volume = 100;
        ymmusic_channels[i].pitch_bend = 128;
    }
    for (i = 0; i < YMMUSIC_MAX_MIDI_TRACKS; ++i)
    {
        ymmusic_midi_tracks[i].start = NULL;
        ymmusic_midi_tracks[i].ptr = NULL;
        ymmusic_midi_tracks[i].end = NULL;
        ymmusic_midi_tracks[i].next = YMMUSIC_MIDI_ENDED;
        ymmusic_midi_tracks[i].running_status = 0;
        ymmusic_midi_tracks[i].active = 0;
    }
}

void ymmusic_init()
{
    ymmusic_reset();
}

#if defined(YMMUSIC_MD)
void ymmusic_md_stop(void)
{
    ymmusic_state = 0;
    ymmusic_silence();
    ymmusic_reset();
}

ymmusic_midi_track_t *ymmusic_md_track(int i)
{
    return &ymmusic_midi_tracks[i];
}

void ymmusic_md_begin(unsigned short division, int tracks, int loop)
{
    ymmusic_midi_division = division ? division : 96;
    ymmusic_midi_num_tracks = tracks;
    ymmusic_mode = 2;
    /* MIDI mode keeps ptr non-null as an active marker */
    ymmusic_data = ymmusic_ptr = (unsigned char *)ymmusic_midi_tracks;
    ymmusic_state = (unsigned short)(YMMUSIC_PLAY | (loop ? YMMUSIC_LOOP : 0));
    ymmusic_midi_reset_tracks();
}

void ymmusic_md_pause(int paused)
{
    if (paused)
    {
        ymmusic_state &= ~YMMUSIC_PLAY;
        ymmusic_silence(); /* no step will now, as none is due */
    }
    else if (ymmusic_ptr)
        ymmusic_state |= YMMUSIC_PLAY;
}

int ymmusic_md_playing(void)
{
    return ymmusic_ptr != NULL && (ymmusic_state & YMMUSIC_PLAY);
}
#endif

// A command not yet taken, or a song playing: worth a step.
int ymmusic_active()
{
    return ymmusic_cmd_nr_end != ymmusic_ack_nr || (ymmusic_state & YMMUSIC_PLAY);
}

// Every voice silent and off, keeping the song and its place.
void ymmusic_silence()
{
    int i;

    for (i = 0; i < 3; i++)
    {
        ym_write(8 + i, 0);
        ym_volume_shadow[i] = 0;
        ym_period_shadow[i] = 0xffff;
        ymmusic_voices[i].ticks = 0xffff;
        ymmusic_voices[i].channel = 0xff;
    }
    ym_mixer(0, 0x3f);
}

#define FIXED_CHANNELS 0
static void ymmusic_play_note(unsigned char channel, unsigned char note, unsigned char use_volume, unsigned char volume)
{
    if (volume > 127)
    {
        volume = 127;
    }
    ymmusic_voice_t *voice = NULL;
    if (channel < FIXED_CHANNELS)
    {
        voice = ymmusic_voices + channel;
        goto found;
    }
    // First try to find a voice that is already playing this channel.
    // It's ok to not support polyphonic channels for now.
    for (int i = FIXED_CHANNELS; i < YMMUSIC_NUMVOICES; i++)
    {
        if (ymmusic_voices[i].channel == channel)
        {
            voice = ymmusic_voices + i;
            goto found;
        }
    }
    // If that fails, try to find a free voice
    for (int i = FIXED_CHANNELS; i < YMMUSIC_NUMVOICES; i++)
    {
        if (ymmusic_voices[i].ticks == 0xffff)
        {
            voice = ymmusic_voices + i;
            goto found;
        }
    }
    // If that fails, try to replace a released note
    for (int i = FIXED_CHANNELS; i < YMMUSIC_NUMVOICES; i++)
    {
        if (ymmusic_voices[i].released)
        {
            voice = ymmusic_voices + i;
            goto found;
        }
    }
    // If it's a primary channel, we can try to replace a secondary channel. Take the oldest one.
    if (channel < 10)
    {
        for (int i = FIXED_CHANNELS; i < YMMUSIC_NUMVOICES; i++)
        {
            // Also a primary channel? Skip!
            if (ymmusic_voices[i].channel < 10)
                continue;
            if (!voice || voice->ticks < ymmusic_voices[i].ticks)
                voice = ymmusic_voices + i;
        }
        if (voice)
            goto found;
    }
    // No appropriate voice?
    return;

    // We have found a voice.
found:

    if (ymmusic_mode == 2)
    {
        // MIDI: the velocity belongs to the note; the channel's volume is
        // controller 7's
        voice->velocity = volume;
    }
    else
    {
        if (use_volume)
        {
            ymmusic_channels[channel].volume = volume;
        }
        voice->velocity = 127;
    }
    voice->channel = channel;
    voice->note = note;
    voice->ticks = 0;
    voice->released = false;

    if (channel == 15) {
        // Percussion channel
        if (note == 36) {
            voice->instrument = &bass_drum;
        } else if (note == 40) {
            voice->instrument = &electric_snare;
        } else {
            voice->instrument = &dummy_percussion;
        }
    } else if (ymmusic_channels[channel].instrument == 29) {
        voice->instrument = &overdriven_guitar;
    } else if (ymmusic_channels[channel].instrument == 30) {
        voice->instrument = &distortion_guitar;
    } else {
        voice->instrument = &dummy_instrument;
    }
    
    if (voice->instrument && voice->instrument->overrides_note) {
        voice->note = voice->instrument->override_note;
    }
}

static void ymmusic_release_note(unsigned char channel, unsigned char note)
{
    for (int i = 0; i < YMMUSIC_NUMVOICES; i++)
    {
        ymmusic_voice_t *voice = ymmusic_voices + i;
        if (voice->channel == channel && voice->note == note)
        {
            voice->ticks = 0;
            voice->released = true;
            break;
        }
    }
}

static void ymmusic_pitch_bend(unsigned char channel, unsigned char pitch_bend)
{
    ymmusic_channels[channel].pitch_bend = pitch_bend;
}

static void ymmusic_controller(unsigned char channel, unsigned char control, unsigned char value)
{
    if (control == 0)
    {
        ymmusic_channels[channel].instrument = value;
    }
    else if (control == 3)
    {
        ymmusic_channels[channel].volume = value;
    }
    else if (control == 4)
    {
        // Pan: ignore
    }
    else
    {
        // fprintf(stderr, "\rC %d %d %d", channel, control, value);
    }
}

// Retrieves the value belonging to the current tick from an envelope.
static signed char ymmusic_envelope_value(envelope_t *env, unsigned short ticks, unsigned char released) {
    if (released) {
        // In release phase
        ticks += env->release_begin;
    } else if (ticks >= env->sustain_begin) {
        // In sustain phase
        unsigned short sustain_len = env->release_begin - env->sustain_begin;
        ticks -= env->sustain_begin;
        if ((sustain_len - 1) & sustain_len) {
            // sustain length is arbitrary number
            ticks %= sustain_len;
        } else {
            // sustain length is power of two
            ticks &= sustain_len - 1;
        }
        ticks += env->sustain_begin;
    }
    if (ticks > env->last) {
        ticks = env->last;
    }
    return env->data[ticks];
}

// Calculates the volume of a voice at the current tick, as a YM level 0..15:
// the channel's volume times the note's velocity, the instrument's envelope
// on that, then the music volume.
static unsigned char ymmusic_voice_volume(ymmusic_voice_t *voice)
{
    short volume = (short)(ym_mulu(ymmusic_channels[voice->channel].volume, voice->velocity) >> 7);
    if (voice->instrument && voice->instrument->volume_envelope) {
        envelope_t *env = voice->instrument->volume_envelope;
        volume += ymmusic_envelope_value(env, voice->ticks, voice->released);
    }
    if (volume < 0) volume = 0;
    if (volume > 127) volume = 127;
    return ymmusic_levels[ym_mulu(volume, ymmusic_master) >> 8];
}

// Calculates the note of a voice at the current tick, in 128th of a note
static short ymmusic_voice_note(ymmusic_voice_t *voice)
{
    short note = voice->note << 7;
    note += (short)ymmusic_channels[voice->channel].pitch_bend - 128;
    if (voice->instrument && voice->instrument->note_envelope) {
        envelope_t *env = voice->instrument->note_envelope;
        note += ymmusic_envelope_value(env, voice->ticks, voice->released) << 7;
    }
    if (voice->instrument && voice->instrument->pitch_envelope) {
        envelope_t *env = voice->instrument->pitch_envelope;
        note += ymmusic_envelope_value(env, voice->ticks, voice->released);
    }
    if (note < 0) {
        note = 0;
    }
    return note;
}

// Determines whether a voice has finished playing.
static int ymmusic_voice_finished(ymmusic_voice_t *voice)
{
    if (voice->released)
    {
        // If there is a volume envelope, it controls how long the voice keeps playing after release.
        if (voice->instrument && voice->instrument->volume_envelope) {
            return voice->ticks > voice->instrument->volume_envelope->last;
        }
        // Otherwise, the voice is finished on release.
        return true;
    } else {
        // If there is a volume_envelope and it has no sustain cycle (release_begin > last), the voice ends on its own.
        if (voice->instrument && voice->instrument->volume_envelope
            && voice->instrument->volume_envelope->release_begin > voice->instrument->volume_envelope->last)
        {
            return voice->ticks > voice->instrument->volume_envelope->last;
        }
        // Otherwise, the voice does not end until released.
        return false;
    }
}

#if !defined(YMMUSIC_MD)
// Debug function to dump a human-readable transcript of the MUS file.
static void ymmusic_dump(unsigned char *data, FILE *f)
{
    unsigned short lenSong = SHORT(*(unsigned short *)(data + 4));
    unsigned short offSong = SHORT(*(unsigned short *)(data + 6));
    fprintf(f, "%d bytes of music at offset %d\n", lenSong, offSong);
    fprintf(f, "%d channels, %d secondary channels, %d instruments\n",
            SHORT(*(unsigned short *)(data + 8)),
            SHORT(*(unsigned short *)(data + 10)),
            SHORT(*(unsigned short *)(data + 12)));
    unsigned char *p = data + offSong;
    short last_channel = -1;
    while (p < p + lenSong + offSong)
    {
        unsigned char last;
        do
        {
            last = (0x80 & *p);
            unsigned char event = (0x70 & *p) >> 4;
            unsigned char channel = *p & 0xf;
            if (channel == last_channel)
            {
                fprintf(f, "       ");
            }
            else
            {
                fprintf(f, " Ch %-2d ", channel);
            }
            last_channel = channel;
            switch (event)
            {
            case 0: // Release note
                fprintf(f, "release note %d", p[1] & 0x7f);
                p += 2;
                break;
            case 1: // Play note
                fprintf(f, "play note %d", p[1] & 0x7f);
                if (p[1] & 0x80)
                    fprintf(f, " at volume %d", p[2]);
                p += p[1] & 0x80 ? 3 : 2;
                break;
            case 2: // Pitch bend
                fprintf(f, "pitch bend %d", p[1]);
                p += 2;
                break;
            case 3: // System event
                fprintf(f, "system event %d", p[1]);
                p += 2;
                break;
            case 4: // Controller
                switch (p[1])
                {
                case 0:
                    fprintf(f, "instrument %d", p[2]);
                    break;
                case 1:
                    fprintf(f, "bank %d", p[2]);
                    break;
                case 2:
                    fprintf(f, "vibrato %d", p[2]);
                    break;
                case 3:
                    fprintf(f, "volume %d", p[2]);
                    break;
                case 4:
                    fprintf(f, "pan %d", p[2]);
                    break;
                case 5:
                    fprintf(f, "expression %d", p[2]);
                    break;
                case 6:
                    fprintf(f, "reverb %d", p[2]);
                    break;
                case 7:
                    fprintf(f, "chorus %d", p[2]);
                    break;
                case 8:
                    fprintf(f, "sustain %d", p[2]);
                    break;
                case 9:
                    fprintf(f, "soft %d", p[2]);
                    break;
                default:
                    fprintf(f, "controller %d %d ", p[1], p[2]);
                    break;
                }
                p += 3;
                break;
            case 5: // End of measure
                fprintf(f, "EOM");
                p += 1;
                break;
            case 6: // Finish
                fprintf(f, "FINISH\n");
                goto finish;
            case 7: // Unused
                fprintf(f, "Unused %d", p[1]);
                p += 2;
                break;
            }
            fprintf(f, "\n");
        } while (!last);

        short delay = 0;
        do
        {
            delay = (delay << 7) + (0x7f & *p);
        } while (0x80 & *p++);
        fprintf(f, "Delay %d\n", delay);
    }
finish:
    return;
}

static void ymmusic_dump_file(unsigned char *data)
{
    FILE *f = fopen("musdump.txt", "w");
    ymmusic_dump(data, f);
    fclose(f);
}
#endif

// Called cyclically to drive the internal playback state and to push commands to YM-2149 hardware.
void ymmusic_update()
{
    if (ymmusic_cmd_nr_end != ymmusic_ack_nr && ymmusic_cmd_nr_end == ymmusic_cmd_nr_begin)
    {
        // A command has been received and it is consistent.
        if (ymmusic_data != ymmusic_data_cmd)
        {
            ymmusic_data = ymmusic_data_cmd;
            ymmusic_ptr = NULL;
            ymmusic_reset();
        }
        if (ymmusic_state != ymmusic_state_cmd)
        {
            ymmusic_state = ymmusic_state_cmd;
        }
        ymmusic_ack_nr = ymmusic_cmd_nr_end;
    }

    // Skip music data header / initialize parser
    if (ymmusic_data != NULL && ymmusic_ptr == NULL)
    {
        if (ymmusic_data[0] == 'M' && ymmusic_data[1] == 'U' && ymmusic_data[2] == 'S' && ymmusic_data[3] == 0x1a)
        {
            // ymmusic_dump_file(ymmusic_data);
            unsigned short lenSong = SHORT(*(unsigned short *)(ymmusic_data + 4));
            unsigned short offSong = SHORT(*(unsigned short *)(ymmusic_data + 6));
            ymmusic_end = ymmusic_data + offSong + lenSong;
            ymmusic_ptr = ymmusic_data + offSong;
            ymmusic_mode = 1;
            ymmusic_wait = 0;
            ymmusic_wait_remainder = 0;
        }
        else if (ymmusic_midi_init(ymmusic_data))
        {
            // MIDI mode uses its own track cursors but keeps ptr non-null as an active marker.
            ymmusic_ptr = ymmusic_data;
            ymmusic_mode = 2;
        }
    }

    // No cursor? Do nothing.
    if (!ymmusic_ptr)
    {
        ymmusic_reset();
        return;
    }

    // Not playing (paused)? Silence, but keep the place to carry on from.
    if (!(ymmusic_state & YMMUSIC_PLAY))
    {
        ymmusic_silence();
        return;
    }

    // We're playing, advance hardware channels
    for (int i = 0; i < 3; i++)
    {
        ymmusic_voice_t *voice = ymmusic_voices + i;
        if (voice->ticks == 0xffff)
        {
            continue;
        }

        if (ymmusic_voice_finished(voice)) {
            voice->ticks = 0xffff;
        } else {
            // Frequency in 128th of a note
            unsigned short note = ymmusic_voice_note(voice);

            short divisor = ymmusic_divisors[note >> 7][(note >> 3) & 15];

            // Push note to soundchip, if it changed
            if (ym_period_shadow[voice->ymidx] != (unsigned short)divisor)
            {
                ym_period_shadow[voice->ymidx] = divisor;
                ym_write(0 + 2 * voice->ymidx, divisor & 0xff);
                ym_write(1 + 2 * voice->ymidx, divisor >> 8);
            }

            // Amplitude, if it changed
            unsigned char new_volume = ymmusic_voice_volume(voice);

            if (ym_volume_shadow[voice->ymidx] != new_volume)
            {
                ym_volume_shadow[voice->ymidx] = new_volume;
                ym_write(8 + voice->ymidx, new_volume);
            }

            // Enable mixer
            if (voice->ticks == 0 && !voice->released)
            {
                // Note just pressed? Enable the voice's tone, and its
                // noise if the instrument has it.
                if (voice->instrument && voice->instrument->enables_noise) {
                    ym_mixer(9 << voice->ymidx, 0);
                } else {
                    ym_mixer(1 << voice->ymidx, 8 << voice->ymidx);
                }
            } 

            voice->ticks++;
        }

        if (voice->ticks == 0xffff)
        {
            // Reaching end? Disable both tone and noise for the voice.
            ym_mixer(0, 9 << voice->ymidx);
        }
    }

    if (ymmusic_mode == 2)
    {
        ymmusic_midi_update();
        return;
    }

    // Waiting? Decrement and do nothing.
    if (ymmusic_wait > 0)
    {
        ymmusic_wait--;
        return;
    }

    // Parse the next event or delay (depending on state) as long as we don't actually have to wait.
    do
    {
        // Save cursor so that we can calculate the time spent reading these bytes.
        unsigned char *ymmusic_ptr_orig = ymmusic_ptr;
        if (ymmusic_state & YMMUSIC_READ_DELAY)
        {
            // Read a delay in 1/140th of a second
            short delay = 0;
            do
            {
                delay = (delay << 7) + (0x7f & *ymmusic_ptr);
            } while (0x80 & *ymmusic_ptr++);
            // Convert delay into 1/512th of 1/55s.
            ymmusic_wait_remainder += 201 * delay;
            // Read regular event next
            ymmusic_state &= ~YMMUSIC_READ_DELAY;
        }
        else
        {
            // Parse a regular event
            if (0x80 & *ymmusic_ptr)
            {
                // Is last regular event? Read delay next.
                ymmusic_state |= YMMUSIC_READ_DELAY;
            }
            unsigned char event = (0x70 & *ymmusic_ptr) >> 4;
            unsigned char channel = *ymmusic_ptr & 0xf;
            switch (event)
            {
            case 0: // Release note
                ymmusic_release_note(channel, ymmusic_ptr[1] & 0x7f);
                ymmusic_ptr += 2;
                break;
            case 1: // Play note
                ymmusic_play_note(channel, ymmusic_ptr[1] & 0x7f, ymmusic_ptr[1] & 0x80, ymmusic_ptr[2]);
                ymmusic_ptr += (ymmusic_ptr[1] & 0x80) ? 3 : 2;
                break;
            case 2: // Pitch bend
                ymmusic_pitch_bend(channel, ymmusic_ptr[1]);
                ymmusic_ptr += 2;
                break;
            case 3: // System event
                ymmusic_ptr += 2;
                break;
            case 4: // Controller
                // fprintf(stderr, "\rC %d %d %d ", channel, ymmusic_ptr[1], ymmusic_ptr[2]);
                ymmusic_controller(channel, ymmusic_ptr[1], ymmusic_ptr[2]);
                ymmusic_ptr += 3;
                break;
            case 5: // End of measure
                ymmusic_ptr += 1;
                break;
            case 6: // Finish
                if (!(ymmusic_state & YMMUSIC_LOOP))
                {
                    ymmusic_state &= ~YMMUSIC_PLAY;
                }
                ymmusic_ptr = NULL;
                break;
            case 7: // Unused
                ymmusic_ptr += 2;
                break;
            }
        }
        // Add delay penalty for number of bytes read. MIDI transfers 3125 bytes/s, that's 9/512 of 1/55s per byte.
        ymmusic_wait_remainder += YMMUSIC_PER_BYTE_PENALTY * (ymmusic_ptr - ymmusic_ptr_orig);

        // Repeat until we have to wait at least one tick.
    } while (ymmusic_wait_remainder < 512 && ymmusic_ptr);

    ymmusic_wait = ymmusic_wait_remainder / 512;
    ymmusic_wait_remainder %= 512;
}
