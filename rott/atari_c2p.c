/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
/*
Atari ST low-res chunky-to-planar conversion with Bayer dithering.
Based on STDOOM's atari_c2p.c, adapted for dynamic palette weighting.
*/
#include <mint/osbind.h>
#include <mint/cookie.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "atari_c2p.h"
#include "atari_eight.h"
#include "atari_palette.h"
#include "rott_md_protocol.h"

typedef struct
{
    unsigned char r;
    unsigned char g;
    unsigned char b;
} Color;

typedef struct
{
    int start;
    int count;
    unsigned char rmin, rmax;
    unsigned char gmin, gmax;
    unsigned char bmin, bmax;
} ColorBox;

static unsigned long c2p_table[4][256][8];
static unsigned long c2p_2x_table[4][256][4];
static unsigned long c2p_4x_table[4][256][2];
/*
 * The same bits again, a colour every 256 bytes, so a byte of the chunky
 * screen masked in place (and.l #$FF00FF00) is already its index: no
 * shift. Indexed around the middle, as d8(An,Xn.w) sign-extends the
 * index: colour c at C2P_WIDE + (short)(c << 8). Each colour's 256 bytes:
 * the 1x table's four phases (32 bytes each), then the 2x table's (16).
 */
static unsigned long c2p_wide_mem[16384];
#define C2P_WIDE ((unsigned char *)c2p_wide_mem + 32768)
#define C2P_WIDE_PAIRS(phase) (C2P_WIDE + 128 + ((phase) << 4))
#define C2P_WIDE_1X(phase) (C2P_WIDE + ((phase) << 5))
static unsigned short saved_palette[16];
static int palette_saved = 0;
static unsigned int last_palette_hash = 0;
static int palette_hash_valid = 0;
static int use_table = 1;     /* atari_c2p_use_table */
static int c2p_fast_mode = 0;

#ifndef ATARI_C2P_STRICT_NO_OVERLAP
#define ATARI_C2P_STRICT_NO_OVERLAP 0
#endif
#ifndef ATARI_NOIR
#define ATARI_NOIR 0
#endif
#ifndef ATARI_NOIR_DITHERING
#define ATARI_NOIR_DITHERING 0
#endif
#ifndef ATARI_C2P_FAST_COPY
#define ATARI_C2P_FAST_COPY 1
#endif
#ifndef ATARI_C2P_DIRTY_TILES
#define ATARI_C2P_DIRTY_TILES 1
#endif
#ifndef ATARI_C2P_DIRTY_TILE_THRESHOLD
#define ATARI_C2P_DIRTY_TILE_THRESHOLD 140
#endif

static Color palette16[16];
static unsigned char weights256[256][16];
#if ATARI_C2P_DIRTY_TILES
static unsigned char prev_chunky[320 * 200];
static int prev_chunky_valid = 0;
#endif

static unsigned short stcolor(unsigned char r, unsigned char g, unsigned char b);

static void build_noir_palette16(Color *out16, unsigned short *stpalette)
{
    int i;
    for (i = 0; i < 16; ++i)
    {
        unsigned char v = (unsigned char)((i * 255 + 7) / 15);
        out16[i].r = v;
        out16[i].g = v;
        out16[i].b = v;
        stpalette[i] = stcolor(v, v, v);
    }
}

static void c2p_1x_lorez(register unsigned char *out, const unsigned char *in, unsigned short pixels, const unsigned char *table);
static void c2p_pair_lorez(register unsigned char *out, const unsigned char *in, unsigned short pixels, const unsigned char *table);

/*
 * The rectangle the renderer says it drew afresh this frame
 * (atari_c2p_view_drawn), as whole 16-pixel groups and 8-row tiles inside
 * it; empty when lx1 <= lx0. Its tiles are converted without a compare and
 * prev_chunky keeps no copy of them, so the rectangle is "stale" there
 * (sx0...) until a frame that is not told the same one converts it once
 * with a copy.
 */
static short lx0, ly0, lx1, ly1;
static short sx0, sy0, sx1, sy1;

void atari_c2p_view_drawn(int x, int y, int w, int h)
{
    lx0 = (short)((x + 15) >> 4);
    lx1 = (short)((x + w) >> 4);
    ly0 = (short)((y + 7) >> 3);
    ly1 = (short)((y + h) >> 3);
    if (ly1 <= ly0)
        lx1 = lx0;
}

/*
 * Low detail's pairs (atari_c2p.h): walls, floors, ceilings and the sky
 * draw every pixel twice, from an even column, so a row of the view is
 * pairs of the same colour but where something drew a single column. Those
 * draws say where (atari_unpair_col, atari_unpair_rect): for each group of
 * 16 columns the screen rows from the first to the last such draw. The
 * view's tiles outside them are converted a pair at a time
 * (c2p_pair_lorez, the 2x table: the same bits as c2p_1x_lorez gives a
 * pair). ST Low only: on a TT or a Falcon STDL's pair conversion measured
 * no faster than its own.
 */
unsigned char *atari_pair_base;
short atari_pair_vx, atari_pair_vy, atari_pair_vw, atari_pair_vbot;
short atari_unpair_top[20], atari_unpair_bot[20];

void atari_c2p_pairs_begin(unsigned char *view, int x, int y, int w, int h)
{
    int g;

    atari_pair_base = NULL;
    if ((x | w) & 1 || x < 0 || w <= 0 || h <= 0 || x + w > 320 || y < 0 || y + h > 200)
        return;
    for (g = 0; g < 20; ++g)
    {
        atari_unpair_top[g] = 32767;
        atari_unpair_bot[g] = -1;
    }
    atari_pair_vx = (short)x;
    atari_pair_vy = (short)y;
    atari_pair_vw = (short)w;
    atari_pair_vbot = (short)(y + h - 1);
    atari_pair_base = view;
}

void atari_unpair_rect(int x0, int x1, int y0, int y1)
{
    int g;

    if (!atari_pair_base)
        return;
    if (x0 < 0)
        x0 = 0;
    if (x1 > 319)
        x1 = 319;
    if (y0 < 0)
        y0 = 0;
    for (g = x0 >> 4; g <= (x1 >> 4); ++g)
    {
        if (atari_unpair_top[g] > y0)
            atari_unpair_top[g] = (short)y0;
        if (atari_unpair_bot[g] < y1)
            atari_unpair_bot[g] = (short)y1;
    }
}

void atari_unpair_view(void)
{
    if (atari_pair_base)
        atari_unpair_rect(atari_pair_vx, atari_pair_vx + atari_pair_vw - 1,
                          atari_pair_vy, atari_pair_vbot);
}

void atari_unpair_col_slow(const unsigned char *buf, int y0, int y1)
{
    const long x = buf - atari_pair_base;

    if (x < 0 || x >= atari_pair_vw)
    {
        atari_unpair_view(); /* not a column of the view's top row */
        return;
    }
    {
        const int g = (int)(x + atari_pair_vx) >> 4;
        const int ys = (y0 > 0 ? y0 : 0) + atari_pair_vy;
        int ye = y1 + atari_pair_vy;

        if (ye > atari_pair_vbot)
            ye = atari_pair_vbot;
        if (ye < ys)
            return; /* nothing drawn */
        if (atari_unpair_top[g] > ys)
            atari_unpair_top[g] = (short)ys;
        if (atari_unpair_bot[g] < ye)
            atari_unpair_bot[g] = (short)ye;
    }
}

/* ViewZoom on an ST: prev_chunky holds the rows outside the zoomed rows
 * zl_y0 to zl_y1 as they are on screen. */
static int zoom_ok, zl_y0, zl_y1;

#if ATARI_C2P_DIRTY_TILES
static void c2p_invalidate_dirty_cache(void)
{
    prev_chunky_valid = 0;
    sx1 = sx0 = 0; /* all of it is converted and copied next */
    zoom_ok = 0;
}

/* A zoomed frame's row outside the zoom: converted only when it changed
 * (keep: prev_chunky has the row on screen), and kept. */
static void zoom_other_row(unsigned char *out_line, const unsigned char *in_line, int line, int keep)
{
    const unsigned long *a = (const unsigned long *)in_line;
    unsigned long *b = (unsigned long *)(prev_chunky + 320 * line);
    int n;

    if (keep)
    {
        for (n = 0; n < 80; n++)
            if (a[n] != b[n])
                break;
        if (n == 80)
            return;
    }
    c2p_1x_lorez(out_line, in_line, 320, C2P_WIDE_1X(line & 3));
    memcpy(b, a, 320);
}
#else
#define c2p_invalidate_dirty_cache() ((void)0)
#endif

static void c2p_copy_line160(unsigned char *dst, const unsigned char *src)
{
#if defined(__m68k__) && ATARI_C2P_FAST_COPY
    short loops = (160 / 16) - 1;
    asm volatile(
        "0:                               \n\t"
        "movem.l    (%[src])+,%d0-%d3     \n\t"
        "movem.l    %d0-%d3,(%[dst])      \n\t"
        "lea        16(%[dst]),%[dst]     \n\t"
        "dbra.w     %[loops],0b           \n\t"
        : [dst] "+a"(dst), [src] "+a"(src), [loops] "+d"(loops)
        :
        : "d0", "d1", "d2", "d3", "memory", "cc");
#else
    memcpy(dst, src, 160);
#endif
}

static void c2p_restore_game_hud_bars(unsigned char *out, const unsigned char *in)
{
    int line;

    for (line = 0; line < 16; ++line)
        c2p_1x_lorez(out + 160 * line, in + 320 * line, 320, C2P_WIDE_1X(line & 3));

    for (line = 184; line < 200; ++line)
        c2p_1x_lorez(out + 160 * line, in + 320 * line, 320, C2P_WIDE_1X(line & 3));
}

#if ATARI_C2P_DIRTY_TILES
/* A tile's row (16 bytes) against the last screen's, as four longs (both
 * even, all a 68000 asks), where memcmp was a call and a byte loop: 1,100
 * cycles, thousands of times a full-screen update (the loading screen's
 * every step, menus). */
static __inline__ int c2p_row16_differs(const unsigned char *a, const unsigned char *b)
{
    const unsigned long *p = (const unsigned long *)a;
    const unsigned long *q = (const unsigned long *)b;

    return p[0] != q[0] || p[1] != q[1] || p[2] != q[2] || p[3] != q[3];
}

#ifndef ATARI_PAIR_VERIFY
#define ATARI_PAIR_VERIFY 0
#endif
#if ATARI_PAIR_VERIFY
/* A test build's check of the marks: a tile taken as pairs that is not is
 * converted as it is and noted in PAIRS.TXT. */
static int c2p_tile_paired(const unsigned char *p)
{
    int r, i;

    for (r = 0; r < 8; ++r, p += 320)
        for (i = 0; i < 16; i += 2)
            if (p[i] != p[i + 1])
                return 0;
    return 1;
}

static void c2p_pair_fault(int g, int y)
{
    static int faults;
    FILE *f;

    if (++faults > 200)
        return;
    f = fopen("PAIRS.TXT", "a");
    if (f)
    {
        fprintf(f, "group %d row %d\n", g, y);
        fclose(f);
    }
}
#endif

static int c2p_try_fullscreen_dirty_1x(unsigned char *out, const unsigned char *in)
{
    enum
    {
        TILE_W = 16,
        TILE_H = 8,
        TILES_X = 320 / TILE_W,
        TILES_Y = 200 / TILE_H
    };
    /* 1: changed, convert and keep a copy; 2: drawn afresh, convert */
    unsigned char dirty[TILES_Y][TILES_X];
    int dirty_tiles = 0;
    int total_tiles = TILES_X * TILES_Y;
    int dirty_threshold = ATARI_C2P_DIRTY_TILE_THRESHOLD;
    int ty;
    const int live = lx1 > lx0;
    const int restale = sx1 > sx0 && (sx0 != lx0 || sx1 != lx1 || sy0 != ly0 || sy1 != ly1);
    const int pairs_on = live && atari_pair_base != NULL;

    atari_pair_base = NULL;
    if (!prev_chunky_valid)
        return 0; /* the caller converts it all, and keeps the copy */

    memset(dirty, 0, sizeof(dirty));

    for (ty = 0; ty < TILES_Y; ++ty)
    {
        int tx;
        int y = ty * TILE_H;
        /* the row's tiles drawn afresh, as one run (no compare), and those
         * with no copy kept while they were */
        const int vx0 = (live && ty >= ly0 && ty < ly1) ? lx0 : TILES_X;
        const int st0 = (restale && ty >= sy0 && ty < sy1) ? sx0 : TILES_X;
        const int st1 = (restale && ty >= sy0 && ty < sy1) ? sx1 : TILES_X;
        for (tx = 0; tx < TILES_X; ++tx)
        {
            int x = tx * TILE_W;
            int row;
            int changed = 0;

            if (tx == vx0)
            {
                memset(&dirty[ty][lx0], 2, (size_t)(lx1 - lx0));
                if (pairs_on)
                {
                    /* low detail: the groups no single column was drawn
                     * into over these rows (atari_unpair_*) */
                    int g;
                    for (g = lx0; g < lx1; ++g)
                        if (atari_unpair_top[g] > y + TILE_H - 1 || atari_unpair_bot[g] < y)
                        {
                            dirty[ty][g] = 3;
#if ATARI_PAIR_VERIFY
                            if (!c2p_tile_paired(in + 320 * y + 16 * g))
                            {
                                dirty[ty][g] = 2;
                                c2p_pair_fault(g, y);
                            }
#endif
                        }
                }
                tx = lx1 - 1;
                continue;
            }
            if (tx >= st0 && tx < st1)
            {
                dirty[ty][tx] = 1;
                dirty_tiles++;
                continue;
            }

            for (row = 0; row < TILE_H; ++row)
            {
                const unsigned char *src = in + ((y + row) * 320) + x;
                const unsigned char *old = prev_chunky + ((y + row) * 320) + x;
                if (c2p_row16_differs(src, old))
                {
                    changed = 1;
                    break;
                }
            }

            if (changed)
            {
                dirty[ty][tx] = 1;
                dirty_tiles++;
            }
        }
    }

    sx0 = lx0;
    sx1 = lx1;
    sy0 = ly0;
    sy1 = ly1;
    if (dirty_tiles == 0 && !live)
        return 1;

    if (dirty_threshold <= 0 || dirty_threshold > total_tiles)
        dirty_threshold = total_tiles;
    if (dirty_tiles > dirty_threshold)
        return 0; /* the caller converts it all, and keeps the copy */

    for (ty = 0; ty < TILES_Y; ++ty)
    {
        int tx = 0;
        while (tx < TILES_X)
        {
            if (!dirty[ty][tx])
            {
                tx++;
                continue;
            }

            {
                int run_start = tx;
                int y = ty * TILE_H;
                int row;
                int x;
                int width;
                const int kind = dirty[ty][tx];
                const int fresh = kind >= 2;
                const int pair = kind == 3;
                /* a run of one kind: changed tiles, ones drawn afresh, or
                 * ones drawn afresh in pairs */
                while (tx < TILES_X && dirty[ty][tx] == kind)
                    tx++;

                x = run_start * TILE_W;
                width = (tx - run_start) * TILE_W;

                for (row = 0; row < TILE_H; ++row)
                {
                    int line = y + row;
                    unsigned char *out_line = out + (160 * line) + (x >> 1);
                    const unsigned char *in_line = in + (320 * line) + x;
                    if (pair)
                        c2p_pair_lorez(out_line, in_line, (unsigned short)width, C2P_WIDE_PAIRS(line & 3));
                    else
                        c2p_1x_lorez(out_line, in_line, (unsigned short)width, C2P_WIDE_1X(line & 3));
                    if (!fresh)
                        memcpy(prev_chunky + (320 * line) + x, in_line, (size_t)width);
                }
            }
        }
    }

    return 1;
}
#endif

static const Color *sort_colors = NULL;
static int sort_channel = 0;

/*
 * Ties are broken by palette index, so the order -- and so the 16 colours --
 * does not depend on the qsort implementation. The MD/ROTT firmware runs this
 * same median cut (sidecart/rp/src/md_video.c) and must arrive at the same
 * palette as the ST, because both halves of the screen share it.
 */
static int color_cmp(const void *a, const void *b)
{
    unsigned char ia = *(const unsigned char *)a;
    unsigned char ib = *(const unsigned char *)b;
    int d;
    if (sort_channel == 0)
        d = (int)sort_colors[ia].r - (int)sort_colors[ib].r;
    else if (sort_channel == 1)
        d = (int)sort_colors[ia].g - (int)sort_colors[ib].g;
    else
        d = (int)sort_colors[ia].b - (int)sort_colors[ib].b;
    return d ? d : (int)ia - (int)ib;
}

static void update_box(ColorBox *box, const Color *colors, const unsigned char *idx)
{
    unsigned char rmin = 255, rmax = 0;
    unsigned char gmin = 255, gmax = 0;
    unsigned char bmin = 255, bmax = 0;
    int i;
    for (i = 0; i < box->count; ++i)
    {
        const Color *c = &colors[idx[box->start + i]];
        if (c->r < rmin)
            rmin = c->r;
        if (c->r > rmax)
            rmax = c->r;
        if (c->g < gmin)
            gmin = c->g;
        if (c->g > gmax)
            gmax = c->g;
        if (c->b < bmin)
            bmin = c->b;
        if (c->b > bmax)
            bmax = c->b;
    }
    box->rmin = rmin;
    box->rmax = rmax;
    box->gmin = gmin;
    box->gmax = gmax;
    box->bmin = bmin;
    box->bmax = bmax;
}

static void build_palette16(const unsigned char *colors, Color *out16)
{
    Color src[256];
    unsigned char idx[256];
    ColorBox boxes[16];
    int box_count = 1;
    int i;

    for (i = 0; i < 256; ++i)
    {
        src[i].r = colors[i * 3 + 0];
        src[i].g = colors[i * 3 + 1];
        src[i].b = colors[i * 3 + 2];
        idx[i] = (unsigned char)i;
    }

    boxes[0].start = 0;
    boxes[0].count = 256;
    update_box(&boxes[0], src, idx);

    while (box_count < 16)
    {
        int best = -1;
        int best_range = -1;
        for (i = 0; i < box_count; ++i)
        {
            int rrange = (int)boxes[i].rmax - (int)boxes[i].rmin;
            int grange = (int)boxes[i].gmax - (int)boxes[i].gmin;
            int brange = (int)boxes[i].bmax - (int)boxes[i].bmin;
            int range = rrange;
            if (grange > range)
                range = grange;
            if (brange > range)
                range = brange;
            if (boxes[i].count > 1 && range > best_range)
            {
                best_range = range;
                best = i;
            }
        }
        if (best < 0)
            break;

        int rrange = (int)boxes[best].rmax - (int)boxes[best].rmin;
        int grange = (int)boxes[best].gmax - (int)boxes[best].gmin;
        int brange = (int)boxes[best].bmax - (int)boxes[best].bmin;
        if (rrange >= grange && rrange >= brange)
            sort_channel = 0;
        else if (grange >= rrange && grange >= brange)
            sort_channel = 1;
        else
            sort_channel = 2;

        sort_colors = src;
        qsort(idx + boxes[best].start, (size_t)boxes[best].count, sizeof(unsigned char), color_cmp);

        int half = boxes[best].count / 2;
        ColorBox newbox;
        newbox.start = boxes[best].start + half;
        newbox.count = boxes[best].count - half;
        boxes[best].count = half;

        update_box(&boxes[best], src, idx);
        update_box(&newbox, src, idx);

        boxes[box_count++] = newbox;
    }

    for (i = 0; i < box_count; ++i)
    {
        unsigned int rsum = 0, gsum = 0, bsum = 0;
        int j;
        for (j = 0; j < boxes[i].count; ++j)
        {
            const Color *c = &src[idx[boxes[i].start + j]];
            rsum += c->r;
            gsum += c->g;
            bsum += c->b;
        }
        if (boxes[i].count > 0)
        {
            out16[i].r = (unsigned char)(rsum / (unsigned int)boxes[i].count);
            out16[i].g = (unsigned char)(gsum / (unsigned int)boxes[i].count);
            out16[i].b = (unsigned char)(bsum / (unsigned int)boxes[i].count);
        }
        else
        {
            out16[i].r = out16[i].g = out16[i].b = 0;
        }
    }

    for (; i < 16; ++i)
    {
        out16[i] = out16[0];
    }
}

/* v * v for -255..255 in one muls.w, not a 32-bit multiply's library
 * call (__mulsi3): the same number. */
static __inline__ long c2p_sq(int v)
{
#if defined(__mc68000__) || defined(__m68k__)
    long r = v;
    __asm__("muls.w %1,%0" : "+d"(r) : "d"(v));
    return r;
#else
    return (long)v * v;
#endif
}

static void build_weights(const unsigned char *colors, int dither)
{
    int i, j;
    for (i = 0; i < 256; ++i)
    {
        int best = -1, second = -1;
        unsigned int bestd = 0xffffffffu, secondd = 0xffffffffu;
        unsigned int r = colors[i * 3 + 0];
        unsigned int g = colors[i * 3 + 1];
        unsigned int b = colors[i * 3 + 2];
        for (j = 0; j < 16; ++j)
        {
            int dr = (int)r - (int)palette16[j].r;
            int dg = (int)g - (int)palette16[j].g;
            int db = (int)b - (int)palette16[j].b;
            unsigned int d = (unsigned int)(c2p_sq(dr) + c2p_sq(dg) + c2p_sq(db));
            if (d < bestd)
            {
                second = best;
                secondd = bestd;
                best = j;
                bestd = d;
            }
            else if (d < secondd)
            {
                second = j;
                secondd = d;
            }
        }

        for (j = 0; j < 16; ++j)
            weights256[i][j] = 0;

        if (best >= 0 && bestd == 0)
        {
            weights256[i][best] = 16;
        }
        else if (best >= 0 && second >= 0 && dither)
        {
            unsigned int denom = bestd + secondd;
            unsigned int w0 = denom ? (16u * secondd) / denom : 16u;
            unsigned int w1 = 16u - w0;
            weights256[i][best] = (unsigned char)w0;
            weights256[i][second] = (unsigned char)w1;
        }
        else if (best >= 0)
        {
            weights256[i][best] = 16;
        }
    }
}

/* The game palette: its 16 colours and each colour's dither from
 * atari_palette.c, chosen offline for what the game draws. */
static void table_palette(int table, unsigned short *stpalette)
{
    const unsigned short *words = table == 1 ? atari_palette_ste_words : atari_palette_st_words;
    const unsigned char(*rgb)[3] = table == 1 ? atari_palette_ste_rgb : atari_palette_st_rgb;
    const unsigned char(*mix)[2] = table == 1 ? atari_palette_ste_mix : atari_palette_st_mix;
    int i;

    for (i = 0; i < 16; ++i)
    {
        palette16[i].r = rgb[i][0];
        palette16[i].g = rgb[i][1];
        palette16[i].b = rgb[i][2];
        stpalette[i] = words[i];
    }
    for (i = 0; i < 256; ++i)
        md_pal_weights(mix[i], weights256[i]); /* as the Accelerator does */
}

void atari_c2p_use_table(int on)
{
    if (on != use_table)
    {
        use_table = on;
        palette_hash_valid = 0; /* the next palette is rebuilt */
    }
}

/* 16 levels a channel (STE, and the TT's and Falcon's ST modes), or the
 * ST's 8: the _VDO cookie. */
static int ste_palette(void)
{
    static int ste = -1;
    if (ste < 0)
    {
        long vdo = 0;
        ste = Getcookie(C__VDO, &vdo) == C_FOUND && vdo >= 0x00010000L;
    }
    return ste;
}

static unsigned short convert_channel(unsigned char v)
{
    unsigned short r = (v & 0xe0) >> 5;
    r |= (v & 0x10) >> 1;
    return r;
}

static unsigned short stcolor(unsigned char r, unsigned char g, unsigned char b)
{
    unsigned short entry = convert_channel(r);
    entry <<= 4;
    entry |= convert_channel(g);
    entry <<= 4;
    entry |= convert_channel(b);
    return entry;
}

static void write_st_palette(const unsigned short *palette)
{
    long old = Super(0);
    volatile unsigned short *reg = (unsigned short *)0xff8240;
    short n;
    for (n = 0; n < 16; ++n)
        *reg++ = *palette++;
    Super(old);
}

/*
 * Fades done with the 16 colour registers alone, so with no new palette to
 * reduce on each step: the registers show each installed colour mixed
 * towards fade_target by fade_amount sixteenths. A palette installed
 * meanwhile keeps the mix, so a faded-out screen stays dark until faded
 * back in (atari_c2p_fade).
 */
static unsigned short installed_palette[16], shown_palette[16];
static unsigned short fade_target;
static int fade_amount;

/* A channel of an ST colour as 0..15 (the STE's extra bit is the lowest). */
static int st_level(unsigned short colour, int shift)
{
    const int n = (colour >> shift) & 15;
    return ((n & 7) << 1) | (n >> 3);
}

static unsigned short mix_st_colour(unsigned short a, unsigned short b, int amount)
{
    unsigned short out = 0;
    int shift;
    for (shift = 0; shift <= 8; shift += 4)
    {
        const int v = (st_level(a, shift) * (16 - amount) + st_level(b, shift) * amount + 8) >> 4;
        out |= (unsigned short)((((v >> 1) & 7) | ((v & 1) << 3)) << shift);
    }
    return out;
}

static void install_st_palette(const unsigned short *palette)
{
    short n;
    for (n = 0; n < 16; ++n)
    {
        installed_palette[n] = palette[n];
        shown_palette[n] = fade_amount ? mix_st_colour(palette[n], fade_target, fade_amount)
                                       : palette[n];
    }
    write_st_palette(shown_palette);
}

/* Each colour's dither as at most two pens, which is all the median cut's
 * weights and the table's hold; the lower-numbered pen takes the low
 * thresholds, as in c2p_color_tables. */
void atari_c2p_pens(unsigned short *words, unsigned char (*mix)[2])
{
    int i, c;

    memcpy(words, installed_palette, sizeof(installed_palette));
    for (i = 0; i < 256; ++i)
    {
        int a = -1, b = -1;
        for (c = 0; c < 16; ++c)
        {
            if (!weights256[i][c])
                continue;
            if (a < 0)
                a = c;
            else
                b = c;
        }
        if (a < 0)
            a = 15; /* cannot happen: the weights add up to 16 */
        if (b < 0)
            b = a;
        mix[i][0] = (unsigned char)((a << 4) | b);
        mix[i][1] = b == a ? 0 : weights256[i][b];
    }
}

void atari_c2p_fade(int r, int g, int b, int amount, int vbls)
{
    unsigned short from[16], to[16];
    int n, k;

#if ATARI_EIGHT
    if (atari_eight)
    {
        atari_eight_fade(r, g, b, amount, vbls);
        return;
    }
#endif

    if (amount < 0)
        amount = 0;
    if (amount > 16)
        amount = 16;
    fade_target = stcolor((unsigned char)r, (unsigned char)g, (unsigned char)b);
    fade_amount = amount;
    for (n = 0; n < 16; ++n)
    {
        from[n] = shown_palette[n];
        to[n] = mix_st_colour(installed_palette[n], fade_target, amount);
    }
    /* From what is on screen now, whatever mix that was, to the new one. */
    for (k = 1; k < vbls; ++k)
    {
        for (n = 0; n < 16; ++n)
            shown_palette[n] = mix_st_colour(from[n], to[n], (16 * k) / vbls);
        Vsync();
        write_st_palette(shown_palette);
    }
    for (n = 0; n < 16; ++n)
        shown_palette[n] = to[n];
    if (vbls > 0)
        Vsync();
    write_st_palette(shown_palette);
}

int atari_c2p_fade_amount(void)
{
#if ATARI_EIGHT
    if (atari_eight)
        return atari_eight_fade_amount();
#endif
    return fade_amount;
}

static void save_st_palette(unsigned short *palette)
{
    long old = Super(0);
    volatile unsigned short *reg = (unsigned short *)0xff8240;
    short n;
    for (n = 0; n < 16; ++n)
        *palette++ = *reg++;
    Super(old);
}

static const unsigned char bayer4[4][4] = {
    {0, 8, 2, 10},
    {12, 4, 14, 6},
    {3, 11, 1, 9},
    {15, 7, 13, 5}};

/* Colour i's c2p table entries from its weights. The colour a pixel gets
 * depends only on its Bayer threshold (0-15): the first of the 16 colours
 * whose span of the weights holds it, -1 if none. So the 16 answers once,
 * where they were worked out again for each of the 64 entries (most of a
 * palette change: 1.5s at 8MHz, at each level start and pause). */
static void c2p_color_tables(const unsigned char *weights, int i)
{
    unsigned long plane[16]; /* the colour's bit in each plane, before px */
    int t, phase;

    for (t = 0; t < 16; ++t)
    {
        unsigned char lwb = 0, upb = 0;
        short c, col = -1;
        unsigned long pdata = 0;

        for (c = 0; c < 16; ++c)
        {
            upb += weights[c];
            if (t >= lwb && t < upb)
            {
                col = c;
                break;
            }
            lwb += weights[c];
        }
        if (col & 1)
            pdata |= 0x01000000;
        if (col & 2)
            pdata |= 0x00010000;
        if (col & 4)
            pdata |= 0x00000100;
        if (col & 8)
            pdata |= 0x00000001;
        plane[t] = pdata;
    }
    for (phase = 0; phase < 4; ++phase)
    {
        const unsigned char *bayer = bayer4[phase];
        int px;

        for (px = 0; px < 8; ++px)
            c2p_table[phase][i][px] = plane[bayer[px & 3]] << (7 - px);
        for (px = 0; px < 4; ++px)
            c2p_2x_table[phase][i][px] = c2p_table[phase][i][px << 1] |
                                         c2p_table[phase][i][(px << 1) + 1];
        for (px = 0; px < 2; ++px)
            c2p_4x_table[phase][i][px] = c2p_table[phase][i][px << 2] |
                                         c2p_table[phase][i][(px << 2) + 1] |
                                         c2p_table[phase][i][(px << 2) + 2] |
                                         c2p_table[phase][i][(px << 2) + 3];
    }
    {
        unsigned long *w = (unsigned long *)(C2P_WIDE + (short)(i << 8));

        for (phase = 0; phase < 4; ++phase)
        {
            int px;

            for (px = 0; px < 8; ++px)
                w[(phase << 3) + px] = c2p_table[phase][i][px];
            for (px = 0; px < 4; ++px)
                w[32 + (phase << 2) + px] = c2p_2x_table[phase][i][px];
        }
    }
}

static void c2p_1x_lorez(register unsigned char *out, const unsigned char *in, unsigned short pixels, const unsigned char *table)
{
    if (pixels < 16)
        return;
    unsigned short groups = pixels / 16 - 1;
    unsigned long pdata = 0;
    unsigned long mask = 0xff00ff00; /* the wide table's index, in place */
    asm volatile(
        "0:                                         \n\t"
        "movem.l    (%[in])+, %%d0-%%d1             \n\t"
        "move.l     %%d0,%%d2                       \n\t"
        "lsl.l      #8,%%d2                         \n\t"
        "and.l      %[mask],%%d2                    \n\t"
        "move.l     12(%[table],%%d2.w), %[pdata]   \n\t"
        "swap       %%d2                            \n\t"
        "or.l       4(%[table],%%d2.w), %[pdata]    \n\t"
        "and.l      %[mask],%%d0                    \n\t"
        "or.l       8(%[table],%%d0.w), %[pdata]    \n\t"
        "swap       %%d0                            \n\t"
        "or.l       (%[table],%%d0.w), %[pdata]     \n\t"
        "move.l     %%d1,%%d2                       \n\t"
        "lsl.l      #8,%%d2                         \n\t"
        "and.l      %[mask],%%d2                    \n\t"
        "or.l       28(%[table],%%d2.w), %[pdata]   \n\t"
        "swap       %%d2                            \n\t"
        "or.l       20(%[table],%%d2.w), %[pdata]   \n\t"
        "and.l      %[mask],%%d1                    \n\t"
        "or.l       24(%[table],%%d1.w), %[pdata]   \n\t"
        "swap       %%d1                            \n\t"
        "or.l       16(%[table],%%d1.w), %[pdata]   \n\t"
        "movep.l    %[pdata], 0(%[out])           \n\t"
        "movem.l    (%[in])+, %%d0-%%d1             \n\t"
        "move.l     %%d0,%%d2                       \n\t"
        "lsl.l      #8,%%d2                         \n\t"
        "and.l      %[mask],%%d2                    \n\t"
        "move.l     12(%[table],%%d2.w), %[pdata]   \n\t"
        "swap       %%d2                            \n\t"
        "or.l       4(%[table],%%d2.w), %[pdata]    \n\t"
        "and.l      %[mask],%%d0                    \n\t"
        "or.l       8(%[table],%%d0.w), %[pdata]    \n\t"
        "swap       %%d0                            \n\t"
        "or.l       (%[table],%%d0.w), %[pdata]     \n\t"
        "move.l     %%d1,%%d2                       \n\t"
        "lsl.l      #8,%%d2                         \n\t"
        "and.l      %[mask],%%d2                    \n\t"
        "or.l       28(%[table],%%d2.w), %[pdata]   \n\t"
        "swap       %%d2                            \n\t"
        "or.l       20(%[table],%%d2.w), %[pdata]   \n\t"
        "and.l      %[mask],%%d1                    \n\t"
        "or.l       24(%[table],%%d1.w), %[pdata]   \n\t"
        "swap       %%d1                            \n\t"
        "or.l       16(%[table],%%d1.w), %[pdata]   \n\t"
        "movep.l    %[pdata], 1(%[out])           \n\t"
        "lea        8(%[out]), %[out]               \n\t"
        "dbra.w     %[groups],0b                    \n\t"
        : [out] "+a"(out), [in] "+a"(in), [pdata] "+d"(pdata), [groups] "+d"(groups)
        : [table] "a"(table), [mask] "d"(mask)
        : "d0", "d1", "d2", "memory");
}

static void c2p_2x_lorez(register unsigned char *out, const unsigned char *in, unsigned short pixels, unsigned long table[][4])
{
    if (pixels < 8)
        return;
    short groups = pixels / 8 - 1;
    unsigned long pdata;
    unsigned long p0;
    unsigned long p1;
    unsigned long mask = 0x00ff00ff << 4;
    asm volatile(
        "0:                                         \n\t"
        "move.l     (%[in])+,%[p0]                  \n\t"
        "move.l     %[p0],%[p1]                     \n\t"
        "lsl.l      #4,%[p1]                        \n\t"
        "and.l      %[mask],%[p1]                   \n\t"
        "move.l     12(%[table],%[p1].w), %[pdata]  \n\t"
        "swap       %[p1]                           \n\t"
        "or.l       4(%[table],%[p1].w), %[pdata]   \n\t"
        "lsr.l      #4,%[p0]                        \n\t"
        "and.l      %[mask],%[p0]                   \n\t"
        "or.l       8(%[table],%[p0].w), %[pdata]   \n\t"
        "swap       %[p0]                           \n\t"
        "or.l       (%[table],%[p0].w), %[pdata]    \n\t"
        "movep.l    %[pdata], 0(%[out])             \n\t"
        "move.l     (%[in])+,%[p0]                  \n\t"
        "move.l     %[p0],%[p1]                     \n\t"
        "lsl.l      #4,%[p1]                        \n\t"
        "and.l      %[mask],%[p1]                   \n\t"
        "move.l     12(%[table],%[p1].w), %[pdata]  \n\t"
        "swap       %[p1]                           \n\t"
        "or.l       4(%[table],%[p1].w), %[pdata]   \n\t"
        "lsr.l      #4,%[p0]                        \n\t"
        "and.l      %[mask],%[p0]                   \n\t"
        "or.l       8(%[table],%[p0].w), %[pdata]   \n\t"
        "swap       %[p0]                           \n\t"
        "or.l       (%[table],%[p0].w), %[pdata]    \n\t"
        "movep.l    %[pdata], 1(%[out])             \n\t"
        "lea        8(%[out]), %[out]               \n\t"
        "dbra.w     %[groups],0b                    \n\t"
        : [out] "+&a"(out), [in] "+&a"(in), [pdata] "=&d"(pdata), [groups] "+&d"(groups), [p0] "=&d"(p0), [p1] "=&d"(p1)
        : [table] "a"(table), [mask] "d"(mask)
        : "memory", "cc");
}

/* Pairs of the same pixel (low detail's view, atari_unpair_*): the first of
 * each pair through the wide table's 2x part, with no shift. */
static void c2p_pair_lorez(register unsigned char *out, const unsigned char *in, unsigned short pixels, const unsigned char *table)
{
    if (pixels < 16)
        return;
    unsigned short groups = pixels / 16 - 1;
    unsigned long pdata;
    unsigned long p0;
    unsigned long mask = 0xff00ff00;
    asm volatile(
        "0:                                         \n\t"
        "move.l     (%[in])+,%[p0]                  \n\t"
        "and.l      %[mask],%[p0]                   \n\t"
        "move.l     4(%[table],%[p0].w), %[pdata]   \n\t"
        "swap       %[p0]                           \n\t"
        "or.l       (%[table],%[p0].w), %[pdata]    \n\t"
        "move.l     (%[in])+,%[p0]                  \n\t"
        "and.l      %[mask],%[p0]                   \n\t"
        "or.l       12(%[table],%[p0].w), %[pdata]  \n\t"
        "swap       %[p0]                           \n\t"
        "or.l       8(%[table],%[p0].w), %[pdata]   \n\t"
        "movep.l    %[pdata], 0(%[out])             \n\t"
        "move.l     (%[in])+,%[p0]                  \n\t"
        "and.l      %[mask],%[p0]                   \n\t"
        "move.l     4(%[table],%[p0].w), %[pdata]   \n\t"
        "swap       %[p0]                           \n\t"
        "or.l       (%[table],%[p0].w), %[pdata]    \n\t"
        "move.l     (%[in])+,%[p0]                  \n\t"
        "and.l      %[mask],%[p0]                   \n\t"
        "or.l       12(%[table],%[p0].w), %[pdata]  \n\t"
        "swap       %[p0]                           \n\t"
        "or.l       8(%[table],%[p0].w), %[pdata]   \n\t"
        "movep.l    %[pdata], 1(%[out])             \n\t"
        "lea        8(%[out]), %[out]               \n\t"
        "dbra.w     %[groups],0b                    \n\t"
        : [out] "+&a"(out), [in] "+&a"(in), [pdata] "=&d"(pdata), [groups] "+&d"(groups), [p0] "=&d"(p0)
        : [table] "a"(table), [mask] "d"(mask)
        : "memory", "cc");
}

static void c2p_4x_lorez(register unsigned char *out, const unsigned char *in, unsigned short pixels, unsigned long table[][2])
{
    if (pixels < 4)
        return;
    short groups = pixels / 4 - 1;
    unsigned long pdata = 0;
    unsigned long mask = 0xff00ff << 3;
    asm volatile(
        "0:                                         \n\t"
        "move.l     (%[in])+, %%d0                  \n\t"
        "move.l     %%d0,%%d1                       \n\t"
        "lsr.l      #5,%%d0                         \n\t"
        "and.l      %[mask],%%d0                    \n\t"
        "lsl.l      #3,%%d1                         \n\t"
        "and.l      %[mask],%%d1                    \n\t"
        "move.l     0(%[table],%%d0.w),%[pdata]     \n\t"
        "or.l       4(%[table],%%d1.w),%[pdata]     \n\t"
        "movep.l    %[pdata],1(%[out])              \n\t"
        "swap       %%d0                            \n\t"
        "swap       %%d1                            \n\t"
        "move.l     (%[table],%%d0.w),%[pdata]      \n\t"
        "or.l       4(%[table],%%d1.w),%[pdata]     \n\t"
        "movep.l    %[pdata],0(%[out])              \n\t"
        "lea        8(%[out]), %[out]               \n\t"
        "dbra.w     %[groups],0b                    \n\t"
        : [out] "+a"(out), [in] "+a"(in), [pdata] "+d"(pdata), [groups] "+d"(groups)
        : [table] "a"(table), [mask] "d"(mask)
        : "d0", "d1", "memory");
}

void atari_c2p_init(void)
{
    c2p_invalidate_dirty_cache();
    /* The 256-colour screen keeps TOS's colours itself (atari_eight.c). */
    if (atari_eight)
        palette_hash_valid = 0; /* a new screen starts black */
    if (!palette_saved && !atari_eight)
    {
        save_st_palette(saved_palette);
        palette_saved = 1;
    }
}

void atari_c2p_shutdown(void)
{
    c2p_invalidate_dirty_cache();
    fade_amount = 0;
    if (palette_saved && !atari_eight)
    {
        install_st_palette(saved_palette);
    }
}

void atari_c2p_set_fast_mode(int enable)
{
    c2p_fast_mode = enable ? 1 : 0;
}

/* The colours atari_c2p_set_palette was last given, 0..255 a channel. */
static unsigned char last_colors[256 * 3];

void atari_c2p_screen_to_chunky(const unsigned char *screen, unsigned char *chunky,
                                int x, int y, int w, int h)
{
    unsigned char pen_colour[16];
    int pen, i, row, col;

#if ATARI_EIGHT
    if (atari_eight)
    {
        atari_eight_to_chunky(chunky, x, y, w, h);
        c2p_invalidate_dirty_cache();
        return;
    }
#endif
    /* Each of the 16 shown colours as the nearest of the 256. */
    for (pen = 0; pen < 16; ++pen)
    {
        long best = 0x7fffffffL;
        for (i = 0; i < 256; ++i)
        {
            const long dr = (long)last_colors[i * 3] - palette16[pen].r;
            const long dg = (long)last_colors[i * 3 + 1] - palette16[pen].g;
            const long db = (long)last_colors[i * 3 + 2] - palette16[pen].b;
            const long d = dr * dr + dg * dg + db * db;
            if (d < best)
            {
                best = d;
                pen_colour[pen] = (unsigned char)i;
            }
        }
    }
    for (row = y; row < y + h; ++row)
    {
        const unsigned short *line = (const unsigned short *)(screen + row * 160);
        unsigned char *out = chunky + row * 320;
        for (col = x; col < x + w; ++col)
        {
            const unsigned short *group = line + ((col >> 4) << 2);
            const int bit = 15 - (col & 15);
            const int p = ((group[0] >> bit) & 1) | (((group[1] >> bit) & 1) << 1) |
                          (((group[2] >> bit) & 1) << 2) | (((group[3] >> bit) & 1) << 3);
            out[col] = pen_colour[p];
        }
    }
    c2p_invalidate_dirty_cache();
}

void atari_c2p_set_palette(const unsigned char *colors)
{
    unsigned char scaled[256 * 3];
    unsigned char maxv = 0;
    unsigned short stpalette[16];
    unsigned int hash = 2166136261u;
    int i, table;
#if defined(__MINT__)
    static int force_bright_palette = 0;
#endif

    for (i = 0; i < 256 * 3; ++i)
    {
        if (colors[i] > maxv)
            maxv = colors[i];
    }
    if (maxv <= 63)
    {
        for (i = 0; i < 256 * 3; ++i)
            scaled[i] = (unsigned char)(colors[i] << 2);
        colors = scaled;
    }
    memcpy(last_colors, colors, sizeof(last_colors));

    for (i = 0; i < 256 * 3; ++i)
    {
        /* FNV-1a; times 16777619 = 2^24 + 2^8 + 2^7 + 2^4 + 2^1 + 1 in
         * shifts, not a 32-bit multiply's library call (__mulsi3). */
        hash ^= (unsigned int)colors[i];
        hash += (hash << 1) + (hash << 4) + (hash << 7) + (hash << 8) + (hash << 24);
    }
    if (palette_hash_valid && hash == last_palette_hash)
    {
        return;
    }
#if ATARI_EIGHT
    if (atari_eight)
    {
        /* The game's own 256: nothing to choose, and the screen's pixels
         * stay as they are. */
        last_palette_hash = hash;
        palette_hash_valid = 1;
        atari_eight_set_palette(colors);
        return;
    }
#endif
    c2p_invalidate_dirty_cache();
    last_palette_hash = hash;
    palette_hash_valid = 1;

    /* The game palette from the table, unless an Accelerator that cannot
     * do the same shares the screen (atari_c2p_use_table). */
    table = use_table && !ATARI_NOIR && hash == atari_palette_hash
                ? (ste_palette() ? 1 : 2)
                : 0;
    if (table)
    {
        table_palette(table, stpalette);
    }
    else
    {
        if (ATARI_NOIR)
        {
            build_noir_palette16(palette16, stpalette);
        }
        else if (force_bright_palette)
        {
            for (i = 0; i < 16; ++i)
            {
                unsigned char v = i * 16;
                palette16[i].r = 0;
                palette16[i].g = v;
                palette16[i].b = v;
                stpalette[i] = stcolor(0, v, v);
            }
        }
        else
        {
            build_palette16(colors, palette16);
            for (i = 0; i < 16; ++i)
                stpalette[i] = stcolor(palette16[i].r, palette16[i].g, palette16[i].b);
        }
        build_weights(colors, (!ATARI_NOIR || ATARI_NOIR_DITHERING));
    }
    install_st_palette(stpalette);

    for (i = 0; i < 256; ++i)
        c2p_color_tables(weights256[i], i);
}

int atari_hud_dirty = 1;

/* The union of the HUD rects drawn since the last conversion (screen
 * pixels, exclusive ends); empty when x1 <= x0. */
static int hud_x0 = 320, hud_y0 = 200, hud_x1, hud_y1;

void atari_hud_touch_rect(int x, int y, int w, int h)
{
    if (x < hud_x0)
        hud_x0 = x < 0 ? 0 : x;
    if (y < hud_y0)
        hud_y0 = y < 0 ? 0 : y;
    if (x + w > hud_x1)
        hud_x1 = x + w > 320 ? 320 : x + w;
    if (y + h > hud_y1)
        hud_y1 = y + h > 200 ? 200 : y + h;
}

#if ATARI_C2P_DIRTY_TILES
/*
 * MD/ROTT: convert the screen outside a view rect that something else (the
 * Multi-device) fills, 16x8 tiles at a time, only where the chunky screen
 * changed. Tiles wholly inside the view are skipped; ones it only clips are
 * converted, and the caller then copies the view over them.
 */
static int c2p_tile_changed(const unsigned char *in, const unsigned char *old)
{
    int row;
    for (row = 0; row < 8; ++row)
    {
        const unsigned long *a = (const unsigned long *)(in + row * 320);
        const unsigned long *b = (const unsigned long *)(old + row * 320);
        if (a[0] != b[0] || a[1] != b[1] || a[2] != b[2] || a[3] != b[3])
            return 1;
    }
    return 0;
}

/* Tiles tx0..tx1-1 of the tile row at y: convert the runs that changed. */
static void c2p_hud_span(unsigned char *out, const unsigned char *in, int y,
                         int tx0, int tx1, int all)
{
    int tx = tx0;
    while (tx < tx1)
    {
        int run_start;
        if (!all && !c2p_tile_changed(in + y * 320 + tx * 16, prev_chunky + y * 320 + tx * 16))
        {
            tx++;
            continue;
        }
        run_start = tx;
        do
        {
            tx++;
        } while (tx < tx1 &&
                 (all || c2p_tile_changed(in + y * 320 + tx * 16, prev_chunky + y * 320 + tx * 16)));
        {
            const int x0 = run_start * 16;
            const int width = (tx - run_start) * 16;
            int row;
#if ATARI_EIGHT
            if (atari_eight)
                atari_eight_convert(in, x0, y, width, 8);
#endif
            for (row = 0; row < 8; ++row)
            {
                const int line = y + row;
                if (!atari_eight)
                    c2p_1x_lorez(out + 160 * line + (x0 >> 1), in + 320 * line + x0,
                                 (unsigned short)width, C2P_WIDE_1X(line & 3));
                memcpy(prev_chunky + 320 * line + x0, in + 320 * line + x0, (size_t)width);
            }
        }
    }
}

void atari_c2p_hud(unsigned char *out, const unsigned char *in,
                   int view_x, int view_y, int view_w, int view_h)
{
    static int last_x = -1, last_y, last_w, last_h;
    static unsigned char frames;
    int all;
    /* The tile columns wholly inside the view: rows it covers skip them in
     * one step rather than visiting every tile (most of the screen). */
    int vx0 = (view_x + 15) >> 4;
    int vx1 = (view_x + view_w) >> 4;
    int tx0 = 0, tx1 = 320 / 16, ty0 = 0, ty1 = 200 / 8;
    int ty;

    /* A view that moved or shrank uncovers tiles the screen still has the
     * old view in, whatever the chunky screen says: convert them all. */
    if (view_x != last_x || view_y != last_y || view_w != last_w || view_h != last_h)
    {
        prev_chunky_valid = 0;
        last_x = view_x;
        last_y = view_y;
        last_w = view_w;
        last_h = view_h;
    }
    all = !prev_chunky_valid;
    /* Comparing the whole HUD costs more than the rest of this put
     * together, so: all of it when something drew without saying where
     * (ATARI_HUD_TOUCH) and every 64th frame in case something drew without
     * saying at all; else just where something drew (_RECT), if anything.
     * (Every 16th was 2% of an 8MHz ST, and the status bars all say.) */
    if (!all && !atari_hud_dirty && (++frames & 63))
    {
        if (hud_x1 <= hud_x0 || hud_y1 <= hud_y0)
            return;
        tx0 = hud_x0 >> 4;
        tx1 = (hud_x1 + 15) >> 4;
        ty0 = hud_y0 >> 3;
        ty1 = (hud_y1 + 7) >> 3;
    }
    atari_hud_dirty = 0;
    hud_x0 = 320;
    hud_y0 = 200;
    hud_x1 = hud_y1 = 0;

    if (vx0 < 0)
        vx0 = 0;
    if (vx1 > 320 / 16)
        vx1 = 320 / 16;
    if (vx1 < vx0)
        vx1 = vx0;
    for (ty = ty0; ty < ty1; ++ty)
    {
        const int y = ty * 8;
        if (y >= view_y && y + 8 <= view_y + view_h)
        {
            c2p_hud_span(out, in, y, tx0, vx0 < tx1 ? vx0 : tx1, all);
            c2p_hud_span(out, in, y, vx1 > tx0 ? vx1 : tx0, tx1, all);
        }
        else
        {
            c2p_hud_span(out, in, y, tx0, tx1, all);
        }
    }
    prev_chunky_valid = 1;
}
#endif

#if ATARI_EIGHT
/*
 * 256 colours: the whole chunky screen onto the TT's or the Falcon's, the
 * groups of 16 pixels that changed. A row's are taken as one span, its
 * first changed group to its last, and rows with the same span as one box,
 * one call of the conversion: a view drawn afresh every frame is one box,
 * and status bars nobody drew on cost a compare.
 */
/*
 * Low detail's view in 256 colours: the rows of groups g0 to g1 no single
 * column was drawn into (atari_unpair_*) a pair at a time, STDL's pair
 * conversion (atari_c2pp.c), and the rest as usual; a run of groups whose
 * marks are the same over rows y to y + h at a time.
 */
static int eight_pairs_on;

static void eight_pairs(const unsigned char *in, int g0, int g1, int y, int h)
{
    /* Bands of rows between the marks' ends, each a run of groups at a
     * time: a run of 4 groups or more with no mark as pairs, the rest as
     * usual (a narrow box costs STDL's loop more than pairs save). */
    const int y1 = y + h;
    int cut[42], n = 0, i, g;

    cut[n++] = y;
    cut[n++] = y1;
    for (g = g0; g < g1; ++g)
    {
        const int t = atari_unpair_top[g], b = atari_unpair_bot[g] + 1;

        if (t > y && t < y1)
            cut[n++] = t;
        if (b > y && b < y1)
            cut[n++] = b;
    }
    for (i = 1; i < n; ++i) /* sort the cuts, a few dozen at most */
    {
        const int c = cut[i];
        int k = i;

        for (; k > 0 && cut[k - 1] > c; --k)
            cut[k] = cut[k - 1];
        cut[k] = c;
    }
    for (i = 0; i + 1 < n; ++i)
    {
        const int r0 = cut[i], r1 = cut[i + 1];

        if (r1 <= r0)
            continue;
        g = g0;
        while (g < g1)
        {
            /* the next run of unmarked groups 4 wide or more, from g */
            int a = g, e;

            for (;;)
            {
                while (a < g1 && atari_unpair_top[a] < r1 && atari_unpair_bot[a] >= r0)
                    ++a;
                e = a;
                while (e < g1 && !(atari_unpair_top[e] < r1 && atari_unpair_bot[e] >= r0))
                    ++e;
                if (e - a >= 4 || e >= g1)
                    break;
                a = e;
            }
            if (e - a < 4)
                a = e = g1; /* none: the rest as usual */
            if (a > g)
                atari_eight_convert(in, g << 4, r0, (a - g) << 4, r1 - r0);
            if (e > a)
            {
#if ATARI_PAIR_VERIFY
                int vy, vx, bad = 0;

                for (vy = r0; vy < r1 && !bad; ++vy)
                    for (vx = a << 4; vx < e << 4; vx += 2)
                        if (in[320 * vy + vx] != in[320 * vy + vx + 1])
                        {
                            c2p_pair_fault(vx >> 4, vy);
                            bad = 1;
                            break;
                        }
                if (bad)
                    atari_eight_convert(in, a << 4, r0, (e - a) << 4, r1 - r0);
                else
#endif
                atari_eight_convert_pairs_box(in, a << 4, (e - a) << 4, r0, r1 - r0);
            }
            g = e;
        }
    }
}

static void eight_box(const unsigned char *in, int g0, int g1, int y, int h, int fresh)
{
#if ATARI_C2P_DIRTY_TILES
    const int x = g0 << 4, w = (g1 - g0) << 4;
    const unsigned char *src = in + 320 * y + x;
    unsigned char *old = prev_chunky + 320 * y + x;

    if (fresh && eight_pairs_on)
    {
        eight_pairs(in, g0, g1, y, h);
        return;
    }
    atari_eight_convert(in, x, y, w, h);
    if (fresh)
        return; /* drawn afresh every frame: no copy to compare with */
    if (w == 320)
    {
        memcpy(old, src, (size_t)320 * h);
    }
    else
    {
        for (; h > 0; --h, src += 320, old += 320)
            memcpy(old, src, (size_t)w);
    }
#endif
}

/* ViewZoom's rows (eight_zoom): skipped here, and last frame's, whose
 * rows prev_chunky has no copy of. */
static int eight_skip0, eight_skip1;
static int eight_last_skip0, eight_last_skip1;

static void eight_screen(const unsigned char *in)
{
#if ATARI_C2P_DIRTY_TILES
    /* The rows drawn afresh (atari_c2p_view_drawn), in rows here; once
     * that rectangle changes or goes, everything is converted and copied
     * the once. So too when ViewZoom's rows change. */
    const int restale = (sx1 > sx0 && (sx0 != lx0 || sx1 != lx1 || sy0 != ly0 || sy1 != ly1)) ||
                        eight_skip0 != eight_last_skip0 || eight_skip1 != eight_last_skip1;
    const int all = !prev_chunky_valid || restale;
    const int live = lx1 > lx0 && !all;
    const int vy0 = ly0 << 3, vy1 = ly1 << 3;

    eight_pairs_on = live && atari_pair_base != NULL;
    atari_pair_base = NULL;
    const unsigned long *a = (const unsigned long *)in;
    const unsigned long *b = (const unsigned long *)prev_chunky;
    int y, bg0 = 0, bg1 = 0, by = 0, bh = 0, bfresh = 0; /* the box so far */

    eight_last_skip0 = eight_skip0;
    eight_last_skip1 = eight_skip1;
    for (y = 0; y < 200; ++y, a += 80, b += 80)
    {
        int g0 = 20, g1 = 0, g, fresh = 0;

        if (y >= eight_skip0 && y < eight_skip1)
        {
            /* ViewZoom's: eight_zoom converts them */
            if (bh)
            {
                eight_box(in, bg0, bg1, by, bh, bfresh);
                bh = 0;
            }
            continue;
        }
        if (all)
        {
            g0 = 0;
            g1 = 20;
        }
        else
        {
            const int in_view = live && y >= vy0 && y < vy1;

            for (g = 0; g < 20; ++g)
            {
                const unsigned long *p = a + (g << 2), *q = b + (g << 2);

                if (in_view && g == lx0)
                {
                    g = lx1 - 1; /* the view's groups: not compared */
                    continue;
                }
                if (p[0] != q[0] || p[1] != q[1] || p[2] != q[2] || p[3] != q[3])
                {
                    if (g < g0)
                        g0 = g;
                    g1 = g + 1;
                }
            }
            if (in_view)
            {
                /* nothing changed beside the view: the view alone, with
                 * no copy kept */
                fresh = g1 <= g0;
                if (lx0 < g0)
                    g0 = lx0;
                if (lx1 > g1)
                    g1 = lx1;
            }
        }
        if (bh && (g1 <= g0 || g0 != bg0 || g1 != bg1 || fresh != bfresh))
        {
            eight_box(in, bg0, bg1, by, bh, bfresh);
            bh = 0;
        }
        if (g1 > g0)
        {
            if (!bh)
            {
                bg0 = g0;
                bg1 = g1;
                by = y;
                bfresh = fresh;
            }
            bh++;
        }
    }
    if (bh)
        eight_box(in, bg0, bg1, by, bh, bfresh);
    prev_chunky_valid = 1;
    sx0 = lx0;
    sx1 = lx1;
    sy0 = ly0;
    sy1 = ly1;
#else
    atari_eight_convert(in, 0, 0, 320, 200);
#endif
}
#endif

static void c2p_screen(unsigned char *out, const unsigned char *in, int zoom, int center_x, int center_y,
                       int view_x, int view_y, int view_w, int view_h,
                       int protect_top, int protect_bottom);

#if ATARI_EIGHT
/*
 * ViewZoom in 256 colours: the small view shown 2 or 4 times its size, in
 * the place and with the clipping the ST's own zoom gives it (c2p_screen,
 * "Viewport-only zoom"), the rest of the screen at 1x as eight_screen
 * converts it. Each zoomed row is built at 320 pixels, the view's pixels
 * repeated over the zoomed span and the screen's own beside it, and
 * converted; a row whose span is the whole width is the same for its
 * repeats, which are copied. 0 where the ST's zoom would not zoom either.
 */
static int eight_zoom(const unsigned char *in, int zoom, int center_x, int center_y,
                      int x0, int y0, int x1, int y1, int protect_top, int protect_bottom)
{
    static unsigned char row[320];
    static unsigned short dup[256];
    const int sh = zoom == 2 ? 1 : 2;
    int view_w, view_h, src_y0, src_w, src_h, dst_x0, dst_y0, dst_x1, dst_y1;
    int safe_top = 0, safe_bottom = 200, line;

    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > 320)
        x1 = 320;
    if (y1 > 200)
        y1 = 200;
    if ((x0 & 15) != 0)
        x0 = (x0 + 15) & ~15;
    if ((x1 & 15) != 0)
        x1 &= ~15;
    view_w = x1 - x0;
    view_h = y1 - y0;
    if (view_w <= 0 || view_h <= 0 || zoom < 2)
        return 0;
    if (x0 == 0 && y0 == 0 && view_w == 320 && view_h == 200)
        return 0; /* the full-screen zoom of the cinematics: not in play */
    if (zoom == 2 ? (view_w < 8 || view_w > 160 || (view_w & 7) != 0)
                  : (view_w < 4 || view_w > 80 || (view_w & 3) != 0))
        return 0;
    src_w = view_w;
    src_h = view_h;
    src_y0 = y0;
#if ATARI_C2P_STRICT_NO_OVERLAP
    if (protect_top > 0)
        safe_top = protect_top;
    if (protect_bottom > 0)
        safe_bottom = 200 - protect_bottom;
    if (safe_bottom <= safe_top)
        return 0;
    if ((src_h << sh) > (safe_bottom - safe_top))
    {
        src_h = (safe_bottom - safe_top) >> sh;
        if (src_h < 1)
            return 0;
    }
    src_y0 = y0 + ((view_h - src_h) >> 1);
#endif
    dst_x0 = center_x - ((src_w << sh) >> 1);
    dst_y0 = center_y - ((src_h << sh) >> 1);
    dst_x1 = dst_x0 + (src_w << sh);
    dst_y1 = dst_y0 + (src_h << sh);
#if ATARI_C2P_STRICT_NO_OVERLAP
    if (dst_y0 < safe_top)
    {
        dst_y0 = safe_top;
        dst_y1 = dst_y0 + (src_h << sh);
    }
    if (dst_y1 > safe_bottom)
    {
        dst_y1 = safe_bottom;
        dst_y0 = dst_y1 - (src_h << sh);
    }
#endif
    if (dst_x0 < 0 || dst_y0 < 0 || dst_x1 > 320 || dst_y1 > 200 || (dst_x0 & 1) != 0)
        return 0;

    if (!dup[1])
    {
        for (line = 0; line < 256; ++line)
            dup[line] = (unsigned short)((line << 8) | line);
    }

    /* the rest at 1x, and the view's rect not taken as drawn afresh */
    lx1 = lx0;
    eight_skip0 = dst_y0;
    eight_skip1 = dst_y1;
    eight_screen(in);
    eight_skip0 = eight_skip1 = 0;

    for (line = dst_y0; line < dst_y1; line += zoom)
    {
        const unsigned char *src = in + 320 * (src_y0 + ((line - dst_y0) >> sh)) + x0;
        unsigned char *d = row + dst_x0;
        const int whole = dst_x0 == 0 && dst_x1 == 320;
        int i, k;

        /* each pixel 2 or 4 times, as words (dst_x0 is even), from a
         * table of each byte doubled */
        if (zoom == 2)
        {
            unsigned short *dw = (unsigned short *)d;
            for (i = src_w; i > 0; --i)
                *dw++ = dup[*src++];
        }
        else
        {
            unsigned short *dw = (unsigned short *)d;
            for (i = src_w; i > 0; --i)
            {
                const unsigned short v = dup[*src++];
                *dw++ = v;
                *dw++ = v;
            }
        }
        for (k = 0; k < zoom && line + k < dst_y1; ++k)
        {
            if (k > 0 && whole)
            {
                atari_eight_copy_row(line, line + k);
                continue;
            }
            if (!whole)
            {
                const unsigned char *own = in + 320 * (line + k);
                memcpy(row, own, (size_t)dst_x0);
                memcpy(row + dst_x1, own + dst_x1, (size_t)(320 - dst_x1));
            }
            if (!(dst_x0 & 15) && !(dst_x1 & 15))
            {
                /* the zoomed span in pairs, the screen's own beside it */
                atari_eight_convert_span(row, 0, dst_x0, line + k);
                atari_eight_convert_pairs(row, dst_x0, dst_x1 - dst_x0, line + k);
                atari_eight_convert_span(row, dst_x1, 320 - dst_x1, line + k);
            }
            else
                atari_eight_convert_row(row, line + k);
        }
    }
    return 1;
}
#endif

void atari_c2p_screen(unsigned char *out, const unsigned char *in, int zoom, int center_x, int center_y,
                      int view_x, int view_y, int view_w, int view_h,
                      int protect_top, int protect_bottom)
{
#if ATARI_EIGHT
    if (atari_eight && zoom > 1 &&
        eight_zoom(in, zoom, center_x, center_y, view_x, view_y, view_x + view_w,
                   view_y + view_h, protect_top, protect_bottom))
    {
        lx1 = lx0;
        return;
    }
#endif
    c2p_screen(out, in, zoom, center_x, center_y, view_x, view_y, view_w, view_h,
               protect_top, protect_bottom);
    lx1 = lx0; /* the renderer says so again for the next frame, or not */
}

static void c2p_screen(unsigned char *out, const unsigned char *in, int zoom, int center_x, int center_y,
                       int view_x, int view_y, int view_w, int view_h,
                       int protect_top, int protect_bottom)
{
    int x0 = view_x;
    int y0 = view_y;
    int x1 = view_x + view_w;
    int y1 = view_y + view_h;
    int line;
    int keep;
    unsigned char sampled[160];

#if ATARI_EIGHT
    /* 256 colours: ViewZoom is atari_c2p_screen's (eight_zoom); here, 1x. */
    if (atari_eight)
    {
        eight_screen(in);
        return;
    }
#endif

    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > 320)
        x1 = 320;
    if (y1 > 200)
        y1 = 200;

    if ((x0 & 15) != 0)
        x0 = (x0 + 15) & ~15;
    if ((x1 & 15) != 0)
        x1 &= ~15;

    view_w = x1 - x0;
    view_h = y1 - y0;

#if ATARI_C2P_DIRTY_TILES
    if (!(zoom <= 1 && x0 == 0 && y0 == 0 && view_w == 320 && view_h == 200))
    {
        if (zoom > 1)
            prev_chunky_valid = 0; /* the zoom keeps rows of its own: zoom_ok */
        else
            c2p_invalidate_dirty_cache();
    }
    else
        zoom_ok = 0;
#endif

    if (view_w <= 0 || view_h <= 0)
        zoom = 1;

    if (c2p_fast_mode && x0 == 0 && y0 == 0 && view_w == 320 && view_h == 200)
    {
        if (zoom >= 8)
        {
            int block = (zoom >= 16) ? 16 : 8;
            int coarse = 320 / block;
            int dup = (coarse > 0) ? (80 / coarse) : 1;
            int yblock;
            int i;

            if (coarse < 1)
                coarse = 1;
            if (dup < 1)
                dup = 1;

            for (yblock = 0; yblock < 200; yblock += block)
            {
                unsigned char *out_line = out + (160 * yblock);
                const unsigned char *sample_row = in + (320 * yblock);
                int out_idx = 0;
                int rep;

                for (i = 0; i < coarse; ++i)
                {
                    unsigned char v = sample_row[i * block];
                    for (rep = 0; rep < dup && out_idx < 80; ++rep)
                        sampled[out_idx++] = v;
                }
                while (out_idx < 80)
                    sampled[out_idx++] = sampled[out_idx - 1];

                c2p_4x_lorez(out_line, sampled, 80, c2p_4x_table[0]);
                for (rep = 1; rep < block && (yblock + rep) < 200; ++rep)
                    c2p_copy_line160(out_line + (160 * rep), out_line);
            }
            return;
        }

        if (zoom == 2)
        {
            int src_w = 160;
            int yblock;
            int i;

            for (yblock = 0; yblock < 200; yblock += 2)
            {
                unsigned char *out_line = out + (160 * yblock);
                const unsigned char *sample_row = in + (320 * yblock);

                for (i = 0; i < src_w; ++i)
                    sampled[i] = sample_row[i << 1];

                c2p_2x_lorez(out_line, sampled, (unsigned short)src_w, c2p_2x_table[0]);
                c2p_copy_line160(out_line + 160, out_line);
            }
            return;
        }

        if (zoom > 2)
        {
            int src_w = 80;
            int yblock;
            int i;

            for (yblock = 0; yblock < 200; yblock += 4)
            {
                unsigned char *out_line = out + (160 * yblock);
                const unsigned char *sample_row = in + (320 * yblock);

                for (i = 0; i < src_w; ++i)
                    sampled[i] = sample_row[i << 2];

                c2p_4x_lorez(out_line, sampled, (unsigned short)src_w, c2p_4x_table[0]);
                c2p_copy_line160(out_line + 160, out_line);
                c2p_copy_line160(out_line + 320, out_line);
                c2p_copy_line160(out_line + 480, out_line);
            }
            return;
        }
    }

    if (zoom <= 1)
    {
#if ATARI_C2P_DIRTY_TILES
        if (x0 == 0 && y0 == 0 && view_w == 320 && view_h == 200)
        {
            if (c2p_try_fullscreen_dirty_1x(out, in))
                return;
        }
#endif
        for (line = 0; line < 200; ++line)
            c2p_1x_lorez(out + 160 * line, in + 320 * line, 320, C2P_WIDE_1X(line & 3));
#if ATARI_C2P_DIRTY_TILES
        memcpy(prev_chunky, in, 320 * 200);
        prev_chunky_valid = 1;
        sx1 = sx0; /* a copy of everything */
#endif
        return;
    }

    /*
     * Full-screen zoom keeps output at 320x200 by downsampling then expanding
     * back into the same full frame (used by cinematic fast paths).
     */
    if (x0 == 0 && y0 == 0 && view_w == 320 && view_h == 200)
    {
        if (zoom == 2)
        {
            int src_w = 160;
            int i;
            {
                int yblock;
                for (yblock = 0; yblock < 200; yblock += 2)
                {
                    unsigned char *out_line = out + (160 * yblock);
                    const unsigned char *sample_row = in + (320 * yblock);
                    for (i = 0; i < src_w; ++i)
                        sampled[i] = sample_row[i << 1];
                    c2p_2x_lorez(out_line, sampled, (unsigned short)src_w, c2p_2x_table[0]);
                    c2p_copy_line160(out_line + 160, out_line);
                }
            }
            return;
        }

        if (zoom > 2)
        {
            int src_w = 80;
            int i;
            {
                int yblock;
                for (yblock = 0; yblock < 200; yblock += 4)
                {
                    unsigned char *out_line = out + (160 * yblock);
                    const unsigned char *sample_row = in + (320 * yblock);
                    for (i = 0; i < src_w; ++i)
                        sampled[i] = sample_row[i << 2];
                    c2p_4x_lorez(out_line, sampled, (unsigned short)src_w, c2p_4x_table[0]);
                    c2p_copy_line160(out_line + 160, out_line);
                    c2p_copy_line160(out_line + 320, out_line);
                    c2p_copy_line160(out_line + 480, out_line);
                }
            }
            return;
        }
        goto full_1x;
    }

    /*
     * Viewport-only zoom:
     * - render whole frame 1x first (HUD, borders, menus untouched)
     * - then overwrite only destination viewport with a scaled copy.
     */
    if (zoom == 2)
    {
        int src_x0 = x0;
        int src_y0 = y0;
        int src_w = view_w;
        int src_h = view_h;
        int dst_x0;
        int dst_y0;
        int dst_x1;
        int dst_y1;
        int safe_top = 0;
        int safe_bottom = 200;
        int i;

        if (view_w < 8 || view_w > 160 || (view_w & 7) != 0)
            goto full_1x;

#if ATARI_C2P_STRICT_NO_OVERLAP
        if (protect_top > 0)
            safe_top = protect_top;
        if (protect_bottom > 0)
            safe_bottom = 200 - protect_bottom;

        if (safe_bottom <= safe_top)
            goto full_1x;

        if ((src_h << 1) > (safe_bottom - safe_top))
        {
            int max_src_h = (safe_bottom - safe_top) >> 1;
            src_h = max_src_h;
            if (src_h < 1)
                goto full_1x;
        }

        src_x0 = x0;
        src_y0 = y0 + ((view_h - src_h) >> 1);
#endif

        dst_x0 = center_x - src_w;
        dst_y0 = center_y - src_h;
        dst_x1 = dst_x0 + (src_w << 1);
        dst_y1 = dst_y0 + (src_h << 1);

#if ATARI_C2P_STRICT_NO_OVERLAP
        if (dst_y0 < safe_top)
        {
            dst_y0 = safe_top;
            dst_y1 = dst_y0 + (src_h << 1);
        }
        if (dst_y1 > safe_bottom)
        {
            dst_y1 = safe_bottom;
            dst_y0 = dst_y1 - (src_h << 1);
        }
#endif

        if (dst_x0 < 0 || dst_y0 < 0 || dst_x1 > 320 || dst_y1 > 200 || (dst_x0 & 1) != 0)
            goto full_1x;
        keep = zoom_ok && zl_y0 == dst_y0 && zl_y1 == dst_y1;
        zoom_ok = 1;
        zl_y0 = dst_y0;
        zl_y1 = dst_y1;

        for (line = 0; line < 200; ++line)
        {
            unsigned char *out_line = out + 160 * line;
            const unsigned char *in_line = in + 320 * line;

            if (line < dst_y0 || line >= dst_y1)
            {
#if ATARI_C2P_DIRTY_TILES
                zoom_other_row(out_line, in_line, line, keep);
#else
                c2p_1x_lorez(out_line, in_line, 320, C2P_WIDE_1X(line & 3));
#endif
                continue;
            }

            if (dst_x0 >= 16)
                c2p_1x_lorez(out_line, in_line, (unsigned short)dst_x0, C2P_WIDE_1X(line & 3));

            /* straight from the view's row (long aligned: x0 is a
             * multiple of 16), where it was copied out a byte at a time */
            c2p_2x_lorez(out_line + (dst_x0 >> 1),
                         in + 320 * (src_y0 + ((line - dst_y0) >> 1)) + src_x0,
                         (unsigned short)src_w, c2p_2x_table[line & 3]);

            if (dst_x1 <= 304)
                c2p_1x_lorez(out_line + (dst_x1 >> 1), in_line + dst_x1, (unsigned short)(320 - dst_x1), C2P_WIDE_1X(line & 3));
        }
#if !ATARI_C2P_STRICT_NO_OVERLAP
        c2p_restore_game_hud_bars(out, in);
#endif
        return;
    }

    {
        int src_x0 = x0;
        int src_y0 = y0;
        int src_w = view_w;
        int src_h = view_h;
        int dst_x0;
        int dst_y0;
        int dst_x1;
        int dst_y1;
        int safe_top = 0;
        int safe_bottom = 200;
        int i;

        if (view_w < 4 || view_w > 80 || (view_w & 3) != 0)
            goto full_1x;

#if ATARI_C2P_STRICT_NO_OVERLAP
        if (protect_top > 0)
            safe_top = protect_top;
        if (protect_bottom > 0)
            safe_bottom = 200 - protect_bottom;

        if (safe_bottom <= safe_top)
            goto full_1x;

        if ((src_h << 2) > (safe_bottom - safe_top))
        {
            int max_src_h = (safe_bottom - safe_top) >> 2;
            src_h = max_src_h;
            if (src_h < 1)
                goto full_1x;
        }

        src_x0 = x0;
        src_y0 = y0 + ((view_h - src_h) >> 1);
#endif

        dst_x0 = center_x - (src_w << 1);
        dst_y0 = center_y - (src_h << 1);
        dst_x1 = dst_x0 + (src_w << 2);
        dst_y1 = dst_y0 + (src_h << 2);

#if ATARI_C2P_STRICT_NO_OVERLAP
        if (dst_y0 < safe_top)
        {
            dst_y0 = safe_top;
            dst_y1 = dst_y0 + (src_h << 2);
        }
        if (dst_y1 > safe_bottom)
        {
            dst_y1 = safe_bottom;
            dst_y0 = dst_y1 - (src_h << 2);
        }
#endif

        if (dst_x0 < 0 || dst_y0 < 0 || dst_x1 > 320 || dst_y1 > 200 || (dst_x0 & 1) != 0)
            goto full_1x;
        keep = zoom_ok && zl_y0 == dst_y0 && zl_y1 == dst_y1;
        zoom_ok = 1;
        zl_y0 = dst_y0;
        zl_y1 = dst_y1;

        for (line = 0; line < 200; ++line)
        {
            unsigned char *out_line = out + 160 * line;
            const unsigned char *in_line = in + 320 * line;

            if (line < dst_y0 || line >= dst_y1)
            {
#if ATARI_C2P_DIRTY_TILES
                zoom_other_row(out_line, in_line, line, keep);
#else
                c2p_1x_lorez(out_line, in_line, 320, C2P_WIDE_1X(line & 3));
#endif
                continue;
            }

            if (dst_x0 >= 16)
                c2p_1x_lorez(out_line, in_line, (unsigned short)dst_x0, C2P_WIDE_1X(line & 3));

            /* straight from the view's row (long aligned: x0 is a
             * multiple of 16), where it was copied out a byte at a time */
            c2p_4x_lorez(out_line + (dst_x0 >> 1),
                         in + 320 * (src_y0 + ((line - dst_y0) >> 2)) + src_x0,
                         (unsigned short)src_w, c2p_4x_table[line & 3]);

            if (dst_x1 <= 304)
                c2p_1x_lorez(out_line + (dst_x1 >> 1), in_line + dst_x1, (unsigned short)(320 - dst_x1), C2P_WIDE_1X(line & 3));
        }
#if !ATARI_C2P_STRICT_NO_OVERLAP
        c2p_restore_game_hud_bars(out, in);
#endif
    }

    return;

full_1x:
    zoom_ok = 0;
    for (line = 0; line < 200; ++line)
        c2p_1x_lorez(out + 160 * line, in + 320 * line, 320, C2P_WIDE_1X(line & 3));
#if ATARI_C2P_DIRTY_TILES
    memcpy(prev_chunky, in, 320 * 200);
    prev_chunky_valid = 1;
#endif
}
