/*
Atari ST low-res chunky-to-planar conversion with Bayer dithering.
Based on STDOOM's atari_c2p.c, adapted for dynamic palette weighting.
*/
#include <mint/osbind.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "atari_c2p.h"

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
static unsigned short saved_palette[16];
static int palette_saved = 0;
static unsigned int last_palette_hash = 0;
static int palette_hash_valid = 0;
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

static void c2p_1x_lorez(register unsigned char *out, const unsigned char *in, unsigned short pixels, unsigned long table[][8]);

#if ATARI_C2P_DIRTY_TILES
static void c2p_invalidate_dirty_cache(void)
{
    prev_chunky_valid = 0;
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
        c2p_1x_lorez(out + 160 * line, in + 320 * line, 320, c2p_table[line & 3]);

    for (line = 184; line < 200; ++line)
        c2p_1x_lorez(out + 160 * line, in + 320 * line, 320, c2p_table[line & 3]);
}

#if ATARI_C2P_DIRTY_TILES
static int c2p_try_fullscreen_dirty_1x(unsigned char *out, const unsigned char *in)
{
    enum
    {
        TILE_W = 16,
        TILE_H = 8,
        TILES_X = 320 / TILE_W,
        TILES_Y = 200 / TILE_H
    };
    unsigned char dirty[TILES_Y][TILES_X];
    int dirty_tiles = 0;
    int total_tiles = TILES_X * TILES_Y;
    int dirty_threshold = ATARI_C2P_DIRTY_TILE_THRESHOLD;
    int ty;

    if (!prev_chunky_valid)
    {
        memcpy(prev_chunky, in, 320 * 200);
        prev_chunky_valid = 1;
        return 0;
    }

    memset(dirty, 0, sizeof(dirty));

    for (ty = 0; ty < TILES_Y; ++ty)
    {
        int tx;
        int y = ty * TILE_H;
        for (tx = 0; tx < TILES_X; ++tx)
        {
            int x = tx * TILE_W;
            int row;
            int changed = 0;

            for (row = 0; row < TILE_H; ++row)
            {
                const unsigned char *src = in + ((y + row) * 320) + x;
                const unsigned char *old = prev_chunky + ((y + row) * 320) + x;
                if (memcmp(src, old, TILE_W) != 0)
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

    if (dirty_tiles == 0)
        return 1;

    if (dirty_threshold <= 0 || dirty_threshold > total_tiles)
        dirty_threshold = total_tiles;
    if (dirty_tiles > dirty_threshold)
    {
        memcpy(prev_chunky, in, 320 * 200);
        return 0;
    }

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
                while (tx < TILES_X && dirty[ty][tx])
                    tx++;

                x = run_start * TILE_W;
                width = (tx - run_start) * TILE_W;

                for (row = 0; row < TILE_H; ++row)
                {
                    int line = y + row;
                    unsigned char *out_line = out + (160 * line) + (x >> 1);
                    const unsigned char *in_line = in + (320 * line) + x;
                    c2p_1x_lorez(out_line, in_line, (unsigned short)width, c2p_table[line & 3]);
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

static int color_cmp(const void *a, const void *b)
{
    unsigned char ia = *(const unsigned char *)a;
    unsigned char ib = *(const unsigned char *)b;
    if (sort_channel == 0)
        return (int)sort_colors[ia].r - (int)sort_colors[ib].r;
    if (sort_channel == 1)
        return (int)sort_colors[ia].g - (int)sort_colors[ib].g;
    return (int)sort_colors[ia].b - (int)sort_colors[ib].b;
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
            unsigned int d = (unsigned int)(dr * dr + dg * dg + db * db);
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

static void install_st_palette(const unsigned short *palette)
{
    long old = Super(0);
    volatile unsigned short *reg = (unsigned short *)0xff8240;
    short n;
    for (n = 0; n < 16; ++n)
        *reg++ = *palette++;
    Super(old);
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

static short bayer4_color(const unsigned char *weights, short numcolors, short phase, short px)
{
    static unsigned char bayer[4][4] = {
        {0, 8, 2, 10},
        {12, 4, 14, 6},
        {3, 11, 1, 9},
        {15, 7, 13, 5}};
    unsigned char bayer_lwb = 0, bayer_upb = 0;
    short c;
    for (c = 0; c < numcolors; ++c)
    {
        bayer_upb += weights[c];
        if (bayer[phase][px & 3] >= bayer_lwb && bayer[phase][px & 3] < bayer_upb)
        {
            return c;
        }
        bayer_lwb += weights[c];
    }
    return -1;
}

static unsigned long bayer4_lorez_pdata(const unsigned char *weights, short phase, short px)
{
    short c = bayer4_color(weights, 16, phase, px);
    unsigned long pdata = 0;
    if (c & 1)
        pdata |= 0x01000000;
    if (c & 2)
        pdata |= 0x00010000;
    if (c & 4)
        pdata |= 0x00000100;
    if (c & 8)
        pdata |= 0x00000001;
    return pdata << (7 - px);
}

static void c2p_1x_lorez(register unsigned char *out, const unsigned char *in, unsigned short pixels, unsigned long table[][8])
{
    if (pixels < 16)
        return;
    unsigned short groups = pixels / 16 - 1;
    unsigned long pdata = 0;
    unsigned long mask = 0x00ff00ff << 5;
    asm volatile(
        "0:                                         \n\t"
        "movem.l    (%[in])+, %%d0-%%d1             \n\t"
        "move.l     %%d0,%%d2                       \n\t"
        "lsl.l      #5,%%d2                         \n\t"
        "and.l      %[mask],%%d2                    \n\t"
        "move.l     12(%[table],%%d2.w), %[pdata]   \n\t"
        "swap       %%d2                            \n\t"
        "or.l       4(%[table],%%d2.w), %[pdata]    \n\t"
        "lsr.l      #3,%%d0                         \n\t"
        "and.l      %[mask],%%d0                    \n\t"
        "or.l       8(%[table],%%d0.w), %[pdata]    \n\t"
        "swap       %%d0                            \n\t"
        "or.l       (%[table],%%d0.w), %[pdata]     \n\t"
        "move.l     %%d1,%%d2                       \n\t"
        "lsl.l      #5,%%d2                         \n\t"
        "and.l      %[mask],%%d2                    \n\t"
        "or.l       28(%[table],%%d2.w), %[pdata]   \n\t"
        "swap       %%d2                            \n\t"
        "or.l       20(%[table],%%d2.w), %[pdata]   \n\t"
        "lsr.l      #3,%%d1                         \n\t"
        "and.l      %[mask],%%d1                    \n\t"
        "or.l       24(%[table],%%d1.w), %[pdata]   \n\t"
        "swap       %%d1                            \n\t"
        "or.l       16(%[table],%%d1.w), %[pdata]   \n\t"
        "movep.l    %[pdata], 0(%[out])             \n\t"
        "movem.l    (%[in])+, %%d0-%%d1             \n\t"
        "move.l     %%d0,%%d2                       \n\t"
        "lsl.l      #5,%%d2                         \n\t"
        "and.l      %[mask],%%d2                    \n\t"
        "move.l     12(%[table],%%d2.w), %[pdata]   \n\t"
        "swap       %%d2                            \n\t"
        "or.l       4(%[table],%%d2.w), %[pdata]    \n\t"
        "lsr.l      #3,%%d0                         \n\t"
        "and.l      %[mask],%%d0                    \n\t"
        "or.l       8(%[table],%%d0.w), %[pdata]    \n\t"
        "swap       %%d0                            \n\t"
        "or.l       (%[table],%%d0.w), %[pdata]     \n\t"
        "move.l     %%d1,%%d2                       \n\t"
        "lsl.l      #5,%%d2                         \n\t"
        "and.l      %[mask],%%d2                    \n\t"
        "or.l       28(%[table],%%d2.w), %[pdata]   \n\t"
        "swap       %%d2                            \n\t"
        "or.l       20(%[table],%%d2.w), %[pdata]   \n\t"
        "lsr.l      #3,%%d1                         \n\t"
        "and.l      %[mask],%%d1                    \n\t"
        "or.l       24(%[table],%%d1.w), %[pdata]   \n\t"
        "swap       %%d1                            \n\t"
        "or.l       16(%[table],%%d1.w), %[pdata]   \n\t"
        "movep.l    %[pdata], 1(%[out])             \n\t"
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
    if (!palette_saved)
    {
        save_st_palette(saved_palette);
        palette_saved = 1;
    }
}

void atari_c2p_shutdown(void)
{
    c2p_invalidate_dirty_cache();
    if (palette_saved)
    {
        install_st_palette(saved_palette);
    }
}

void atari_c2p_set_fast_mode(int enable)
{
    c2p_fast_mode = enable ? 1 : 0;
}

void atari_c2p_set_palette(const unsigned char *colors)
{
    unsigned char scaled[256 * 3];
    unsigned char maxv = 0;
    unsigned short stpalette[16];
    unsigned int hash = 2166136261u;
    int i;
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

    for (i = 0; i < 256 * 3; ++i)
    {
        hash ^= (unsigned int)colors[i];
        hash *= 16777619u;
    }
    if (palette_hash_valid && hash == last_palette_hash)
    {
        return;
    }
    c2p_invalidate_dirty_cache();
    last_palette_hash = hash;
    palette_hash_valid = 1;

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
    install_st_palette(stpalette);

    for (i = 0; i < 256; ++i)
    {
        unsigned char *weights = weights256[i];
        int phase;
        for (phase = 0; phase < 4; ++phase)
        {
            int px;
            for (px = 0; px < 8; ++px)
            {
                c2p_table[phase][i][px] = bayer4_lorez_pdata(weights, phase, px);
            }
            for (px = 0; px < 4; ++px)
            {
                unsigned long pdata = 0;
                int opx;
                for (opx = px << 1; opx < (px << 1) + 2; ++opx)
                    pdata |= bayer4_lorez_pdata(weights, phase, opx);
                c2p_2x_table[phase][i][px] = pdata;
            }
            for (px = 0; px < 2; ++px)
            {
                unsigned long pdata = 0;
                int opx;
                for (opx = px << 2; opx < (px << 2) + 4; ++opx)
                    pdata |= bayer4_lorez_pdata(weights, phase, opx);
                c2p_4x_table[phase][i][px] = pdata;
            }
        }
    }
}

void atari_c2p_screen(unsigned char *out, const unsigned char *in, int zoom, int center_x, int center_y,
                      int view_x, int view_y, int view_w, int view_h,
                      int protect_top, int protect_bottom)
{
    int x0 = view_x;
    int y0 = view_y;
    int x1 = view_x + view_w;
    int y1 = view_y + view_h;
    int line;
    unsigned char sampled[160];

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
        c2p_invalidate_dirty_cache();
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
            c2p_1x_lorez(out + 160 * line, in + 320 * line, 320, c2p_table[line & 3]);
#if ATARI_C2P_DIRTY_TILES
        memcpy(prev_chunky, in, 320 * 200);
        prev_chunky_valid = 1;
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

        for (line = 0; line < 200; ++line)
        {
            unsigned char *out_line = out + 160 * line;
            const unsigned char *in_line = in + 320 * line;

            if (line < dst_y0 || line >= dst_y1)
            {
                c2p_1x_lorez(out_line, in_line, 320, c2p_table[line & 3]);
                continue;
            }

            if (dst_x0 >= 16)
                c2p_1x_lorez(out_line, in_line, (unsigned short)dst_x0, c2p_table[line & 3]);

            {
                int sample_y = src_y0 + ((line - dst_y0) >> 1);
                const unsigned char *sample_row = in + 320 * sample_y;
                for (i = 0; i < src_w; ++i)
                    sampled[i] = sample_row[src_x0 + i];
            }
            c2p_2x_lorez(out_line + (dst_x0 >> 1), sampled, (unsigned short)src_w, c2p_2x_table[line & 3]);

            if (dst_x1 <= 304)
                c2p_1x_lorez(out_line + (dst_x1 >> 1), in_line + dst_x1, (unsigned short)(320 - dst_x1), c2p_table[line & 3]);
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

        for (line = 0; line < 200; ++line)
        {
            unsigned char *out_line = out + 160 * line;
            const unsigned char *in_line = in + 320 * line;

            if (line < dst_y0 || line >= dst_y1)
            {
                c2p_1x_lorez(out_line, in_line, 320, c2p_table[line & 3]);
                continue;
            }

            if (dst_x0 >= 16)
                c2p_1x_lorez(out_line, in_line, (unsigned short)dst_x0, c2p_table[line & 3]);

            {
                int sample_y = src_y0 + ((line - dst_y0) >> 2);
                const unsigned char *sample_row = in + 320 * sample_y;
                for (i = 0; i < src_w; ++i)
                    sampled[i] = sample_row[src_x0 + i];
            }
            c2p_4x_lorez(out_line + (dst_x0 >> 1), sampled, (unsigned short)src_w, c2p_4x_table[line & 3]);

            if (dst_x1 <= 304)
                c2p_1x_lorez(out_line + (dst_x1 >> 1), in_line + dst_x1, (unsigned short)(320 - dst_x1), c2p_table[line & 3]);
        }
#if !ATARI_C2P_STRICT_NO_OVERLAP
        c2p_restore_game_hud_bars(out, in);
#endif
    }

    return;

full_1x:
    for (line = 0; line < 200; ++line)
        c2p_1x_lorez(out + 160 * line, in + 320 * line, 320, c2p_table[line & 3]);
#if ATARI_C2P_DIRTY_TILES
    memcpy(prev_chunky, in, 320 * 200);
    prev_chunky_valid = 1;
#endif
}
