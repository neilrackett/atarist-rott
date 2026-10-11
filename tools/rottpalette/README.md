# rottpalette

The 16 colours `ROTT_ST.TOS` and the ROTT Accelerator show ROTT's game
palette with, and how each of its 256 colours is dithered from them:
`rott/atari_palette.c`, made by `rottpalette.py`. The game palette is the
only one in play, and the menus use it too; any other palette (the Apogee
logo) is still quantised when it is set, by the median cut in
`atari_c2p.c`.

Needs Python 3 with numpy, and Pillow for `preview`.

## Why

The median cut counts all 256 colours alike, but most of ROTT's palette is
ramps of reds, oranges, yellows and purples that the game seldom draws,
while most of the screen is a few dozen greys, browns and skin tones. It
gave 6 of the 16 colours to reds, oranges and yellows and none to blue, so
greys and the sky were dithered from purple-grey and dark green. And each
colour was dithered between its two nearest pens by distance, whether or
not it lay between them, so only exact matches were solid.

## How

- **Usage** (`usage.txt`): how much the game draws each colour, counted in
  frames captured in EmuMD. The weight of a colour is the square root of
  its count, so large areas still count most but a small bright thing (the
  status bar's heart, an explosion) is not drowned.
- **Colours**: in OKLab, where distance is how different colours look,
  mixed in linear light as the eye blends dither. A weighted median cut
  and k-means give a start, which moves one colour at a time to whichever
  neighbouring colour the machine can show lowers the error of every
  colour's best dither most, until none does. The STE (and the TT's and
  Falcon's ST modes) has 16 levels a channel, the ST 8; each has its own
  table. Hue counts double on the ST, whose dark greys are far apart: at
  the STE's weight it dithered them from dark red and teal.
- **Dither**: for each colour, the pair of pens and the share of a 4x4
  Bayer cell (in 16ths) whose mix is closest, with a cost for high
  contrast pairs (`--mix-penalty`), so colours near a pen come out solid.
  The pens are sorted dark to light, so the cell's low thresholds get the
  darker pen and neighbouring colours' patterns nest.

On frames from all eight shareware levels, the title and the main menu,
the error after a slight blur (OKLab x100) fell from 4.3 to 1.1, and the
status bars' from 4.4 to 2.0, with less visible dither.

## Use

`atari_palette.c` is generated: change the tool or `usage.txt`, not it.

```
python3 tools/rottpalette/rottpalette.py generate \
    --wad tmp/ROTT/HUNTBGIN.WAD --usage tools/rottpalette/usage.txt \
    -o rott/atari_palette.c
```

It takes a minute or two. The palette's hash is in the file, and
`ROTT_ST.TOS` only uses the table for a palette with that hash, so a WAD
with a different palette gets the median cut. Only `ROTT_ST.TOS` compiles
it: the ST sends the ROTT Accelerator how it shows each palette
(`MD_PAL_PENS`), so a new table needs no new firmware.

`preview` renders frames as the original, the STE and the ST:

```
python3 tools/rottpalette/rottpalette.py preview --wad tmp/ROTT/HUNTBGIN.WAD \
    --table rott/atari_palette.c -o tmp/preview FRAME...
```

## Capturing frames

- **The 3D view**: run the firmware in EmuMD with
  `-O chunky=DIR` (from `sidecart/`: `emu/emumd/tools/mdfw run -O chunky=DIR
  ...`), which writes each view as rendered, `cNNNNN_WxH.raw`: the
  768-byte palette, then the pixels. `usage.txt` came from
  `ATARI_LOGIC_CHECK=2` builds (a scripted game: walking, shooting, doors)
  on each of the eight levels, with `startlevel` in `rt_main.c` set to the
  level and `ATARI_LOGIC_CHECK_TICS` in `atari_check.c` raised, plus
  `ATARI_MD_AUTOTEST=8` runs turning on the spot at the start of five.
- **The status bars**: `ATARI_MD_AUTOTEST` builds also write the chunky
  screen as `CHUNKnnn.RAW` (320x200) beside each `SHOTnnn.PI1`.
- **The title and the main menu**: captured once from the chunky screen
  of a normal build (`screenpixels` in `I_FinishUpdate`, `modexlib.c`).

```
python3 tools/rottpalette/rottpalette.py usage -o tools/rottpalette/usage.txt \
    --play VIEWDIR... --hud CHUNK064.RAW... --menu TITLE.RAW MENU.RAW
```

`--play` takes a folder a level, each averaged; `generate --game` and
`--menus` weigh a game screen (view and bars) against the menu screens
(5 to 1 by default).
