/*
 * tools/bench/musictest.c: rott/audiolib/atari_music.c on the host, built
 * as the Accelerator's firmware builds it (YMMUSIC_MD: the registers in
 * ymmusic_regs), every track always ready. Plays each MIDI song in a WAD,
 * looping, for TICKS 50Hz steps and writes the 14 registers after each
 * step, so two versions of the player can be compared byte for byte:
 *
 *   cc -O2 -DYMMUSIC_MD -Irott/audiolib -o /tmp/mt tools/bench/musictest.c \
 *      rott/audiolib/atari_music.c
 *   /tmp/mt tmp/ROTT/HUNTBGIN.WAD out.bin
 *
 * (tools/bench/musictest.sh does that for the working tree and a ref.)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "atari_music.h"

#define TICKS 15000

int YMMUSIC_TRACK_READY(const ymmusic_midi_track_t *t)
{
   (void)t;
   return 1;
}

int ymmusic_md_rewind(void)
{
   return 1;
}

extern unsigned char *ymmusic_data_cmd;
extern unsigned short ymmusic_state_cmd, ymmusic_cmd_nr_begin, ymmusic_cmd_nr_end;
void ymmusic_update(void);

static unsigned long le32(const unsigned char *p)
{
   return p[0] | (p[1] << 8) | ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

int main(int argc, char **argv)
{
   FILE *f, *out;
   long size, n, i;
   unsigned char *wad;
   int songs = 0;

   if (argc < 3 || !(f = fopen(argv[1], "rb")) || !(out = fopen(argv[2], "wb")))
      return 1;
   fseek(f, 0, SEEK_END);
   size = ftell(f);
   fseek(f, 0, SEEK_SET);
   wad = malloc(size);
   if (fread(wad, 1, size, f) != (size_t)size)
      return 1;
   n = (long)le32(wad + 4);
   for (i = 0; i < n; i++)
   {
      const unsigned char *d = wad + le32(wad + 8) + 16 * i;
      unsigned long pos = le32(d), len = le32(d + 4);
      unsigned char *song;
      long t;

      if (len < 14 || memcmp(wad + pos, "MThd", 4))
         continue;
      song = malloc(len);
      memcpy(song, wad + pos, len);
      ++ymmusic_cmd_nr_begin;
      ymmusic_data_cmd = song;
      ymmusic_state_cmd = YMMUSIC_PLAY | YMMUSIC_LOOP;
      ++ymmusic_cmd_nr_end;
      for (t = 0; t < TICKS; t++)
      {
         ymmusic_update();
         fwrite(ymmusic_regs, 1, 14, out);
      }
      /* stopped, as the game stops a song for the next */
      ++ymmusic_cmd_nr_begin;
      ymmusic_state_cmd = 0;
      ++ymmusic_cmd_nr_end;
      ymmusic_update();
      fwrite(ymmusic_regs, 1, 14, out);
      songs++;
      fprintf(stderr, "%.8s ", (const char *)d + 8);
   }
   fprintf(stderr, "\n%d songs, %d steps each\n", songs, TICKS);
   fclose(out);
   return 0;
}
