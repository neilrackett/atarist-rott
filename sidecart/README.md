# ROTT Accelerator

Microfirmware for the [SidecarTridge Multi-device](https://sidecartridge.com)
that renders Rise of the Triad's 3D view for the Atari ST port in this
repository (`ROTT_ST.TOS`). In the source it goes by MD/ROTT for short.

The ST keeps running the game: logic, input, sound effects, HUD, menus. For each
frame it sends the Multi-device what changed (doors, pushwalls, lights,
the view, the objects in sight) over the cartridge port; the
Multi-device's RP2040, at 400 MHz, ray-casts and draws the view with
ROTT's own renderer (walls, floors, ceilings, sky, sprites, lighting,
messages), dithers it to the ST's 16 colours and converts it to bitplanes
in the cartridge ROM window, where the ST copies it to the screen. It
plays ROTT's music as well, with the ST build's own YM2149 player, and
the ST copies the sound chip's registers from it each VBL.

Without the Multi-device, or if anything goes wrong, `ROTT_ST.TOS` falls
back to its own renderer, so one executable serves both.

## Installation

You need a SidecarTridge Multi-device with a microSD card, and the
shareware `HUNTBGIN.WAD` (with `HUNTBGIN.RTL`, `HUNTBGIN.RTC` and
`REMOTE1.RTS` next to `ROTT_ST.TOS` on the ST, as usual).

1. Build the firmware (`make -C sidecart build`, see below) or take the
   `.uf2` and `.json` from `dist/`.
2. Copy both files into `/apps` on the Multi-device's microSD card.
3. Copy `HUNTBGIN.WAD` into `/rott` on the same card. It must be the same
   file the ST uses: the firmware checks its size and directory against
   the ST's copy and refuses a different one.
4. On the Booster screen, press ESC for the app list and select ROTT
   Accelerator. The ST restarts and prints this during boot, with the
   firmware's version (such as `v1.4.0-beta.4`), and `not responding` in
   place of `ready` if the firmware isn't up:

   ```
   ROTT Accelerator <version> ready
   GPLv3 (c)2026 Neil Rackett

   Get ROTT from neilrackett.com/atarist
   and run ROTT_ST.TOS
   ```

5. Run `ROTT_ST.TOS` from disk as normal. At startup it prints
   `ROTT Accelerator <version>` when it will use the Multi-device, or the
   reason it will not, e.g. `ROTT Accelerator: /rott/HUNTBGIN.WAD missing`
   followed by `Using the ST renderer`.

To go back to Booster, power on the ST while holding the SELECT button on
the Multi-device.

The first time you enter a level, the Multi-device copies that level's
graphics from the WAD into its flash: ROTT's view shows
`Preparing level N%` for a few seconds. Entering the same level again
(dying, loading a game) reuses the pack. Graphics that do not fit are
loaded from the SD card the first time they are drawn, so an object may
appear a frame late the first time you meet it.

## Troubleshooting

If `ROTT_ST.TOS` doesn't use the Multi-device, or the view looks wrong,
two test programs narrow it down (`make -C sidecart tests`, in
`sidecart/tests/`). Run them in this order:

1. **`PINGTEST.TOS`**: detection, command round trips and a checksummed
   upload at several sizes. Should end `PASS`. Writes `PINGTEST.TXT` next
   to itself.
2. **`UPTEST.TOS`** (ST low resolution): the Multi-device draws a test
   pattern (a frame, checks or a grey ramp, and a moving red bar) as full
   frames, which the ST copies to the screen, at two sizes, with and
   without pipelining. Should end `PASS` and report frames per second.
   Writes `UPTEST.TXT`.

On a Mega STE the cache is switched off around every cartridge access
(the CPU stays at 16 MHz), as in STDOOM, and only then: building each
frame and converting the HUD run with it on.

When reporting a problem, the most useful things to send are
`PINGTEST.TXT`, `UPTEST.TXT` and a debug firmware's serial log:
`make -C sidecart debug` builds one (it prints a status line every 64
frames and every lump it could not load) and `make -C sidecart uart`
opens the console.

## Things to know

- **Lighting.** The Multi-device lights the view as ROTT does: darker
  areas, darker far walls with light diminishing on (Options menu), one
  side of each wall shaded. The ST renderer flattens this to save time.
  Dark areas therefore look much darker than on the ST renderer, and on
  16 colours the darkest shades come out close to black.
- **Largest view.** The Multi-device's frame buffers stop at 320 x 168,
  the full width between the two status bars; bigger view sizes are
  capped to that while it is in use.
- **Frame rate** is set by the ST, and mostly by ROTT's game logic, which
  stays on the ST: about 70% of a 16 MHz Mega STE's time at the start of
  E1L1 (36 lift disks, moving walls, patrols), against about 25% for
  everything the Multi-device needs from it each frame. On an STE or Mega
  STE the blitter copies each frame while the CPU goes on with the next
  (`ATARI_MD_BLIT`, on unless a test copy at startup disagrees with the
  CPU); the HUD is converted only where something drew; masked walls are
  compared in full only when one changed (`MD_MASKED_TOUCH`), and doors
  and moving walls only while they move or change state, with a few of
  the rest checked each frame in turn; an object is sent if one bit of
  the MD's spotvis (with the tiles around the player added) is set.
- **Matching builds.** `ROTT_ST.TOS` and the firmware must come from the
  same sources (`MD_PROTOCOL_VERSION`); with a mismatch the ST says so and
  uses its own renderer.

## Building

The firmware needs the Arm GNU toolchain and the submodules
(`git submodule update --init --recursive` in the repository root; pico-sdk
2.2.0, pico-extras and FatFs are pinned by `rp/build.sh`). The ST parts are
built with `stcmd` (atarist-toolkit-docker) as the game is.

```sh
export PICO_TOOLCHAIN_PATH=/path/to/arm-none-eabi/bin
make -C sidecart build     # release firmware -> dist/<uuid>.uf2 + .json
make -C sidecart debug     # debug firmware (serial console), bumps the patch version
make -C sidecart tests     # PINGTEST.TOS, UPTEST.TOS in sidecart/tests/
make -C sidecart census    # WAD sections vs the flash budget
make st                    # the game: dist/ROTT_ST.TOS (make sidecart = make -C sidecart build)
```

`ATARI_MD_RENDER=0` builds `ROTT_ST.TOS` without any of this;
`ATARI_MD_PIPELINE=0` makes the ST wait for each frame instead of
overlapping it with the next.

## How it works

| Where | What |
| --- | --- |
| `include/rott_md_protocol.h` | The wire protocol, shared by both sides: ROM4 layout, commands, records |
| `../rott/atari_md.c` | The ST side: level snapshot, per-frame deltas and view, frame copy |
| `../rott/sidecart_md.c`, `sidecart_stubs.S` | ROM3 command transport (from STDOOM) with retries and the Mega STE cache guard |
| `rp/src/md_proto.c` | Decodes ROM3 commands in an interrupt into a queue |
| `rp/src/md_main.c` | Runs the commands: level setup, world updates, frames |
| `rp/src/rott/` | ROTT's renderer (engine, walls, planes, sprites, text) against a mirror of the ST's world |
| `rp/src/md_video.c` | The ST's 16-colour palette choice and dither, identical on both sides, and chunky-to-planar |
| `rp/src/md_pack.c` | Level packs in flash, and the demand-loading ring |
| `rp/src/md_music.c` | Music: the ST build's player (`../rott/audiolib/atari_music.c`) on a timer, the song read from the WAD |
| `target/atarist/` | The 1 KB cartridge header and boot message |

**Level packs.** The flash window for graphics is 896 KB; one shareware
level can ask for 1.6 MB, and the whole WAD holds 4.5 MB. When a level
starts, the ST sends the list of lumps ROTT would precache; the firmware
copies what fits from the WAD on the SD card into flash, most-read first
(colour maps, flats and sky, walls and doors, then sprites, then weapon
frames), keeping at least 192 KB free. That remainder is a ring: a lump
the renderer asks for that is not in flash is read from the SD card into
it (straight away if there is erased space, otherwise between frames,
when erasing a 4 KB sector can drop older lumps without pulling one out
from under the renderer).

**Music.** When the firmware can play music (`MD_CAP_MUSIC` in the
status block, set when it has the WAD open), `MU_PlaySong` sends
`MD_CMD_MUSIC` with the song's lump instead of loading the song on the
ST. The firmware reads the song from the WAD on the SD card a 256-byte
window per MIDI track at a time (in the USB controller's RAM, which the
firmware doesn't otherwise use), and a timer on core 1 steps the ST
build's player every 20 ms. Each step's 14 YM registers go into a ring
of four slots in ROM4 with a mask of those that changed; the ST's VBL
takes the next step from it and writes the changed registers to the
chip (about 1% of an 8 MHz ST). Firmware without it leaves the
capability word 0 and the ST plays the music itself, as before.
`ATARI_MD_MUSIC=0` builds `ROTT_ST.TOS` without this.

**Frames.** Two frame buffers live in the ROM4 window. With pipelining,
the Multi-device draws frame N while the ST copies frame N-1 and runs the
game for N+1. A frame that is late (more than 250 ms) is simply skipped;
after repeated failures the ST switches to its own renderer and says so.

## Testing without hardware

The firmware also builds for [EmuMD](https://github.com/neilrackett/emumd),
which runs Multi-device firmware in a patched Hatari. EmuMD is a submodule
in `emu/emumd`; `mdfw.ini` says how to build the firmware for it, and the
glue is in `emu/`.

From the repository root, `make emu` builds the game (with stcmd) and the
firmware and runs them; `EMU_ARGS` are passed to `mdfw run`. By hand:

```sh
cd sidecart
emu/emumd/tools/mdfw hatari   # once: patched Hatari + EmuTOS in ~/.cache/emumd
emu/emumd/tools/mdfw build    # build/rott-accelerator.mdfw
emu/emumd/tools/mdfw run      # dist/ROTT_ST.TOS, 16 MHz Mega STE
```

The emulated SD card is `tmp/sd`, so the WAD goes in
`tmp/sd/rott/HUNTBGIN.WAD`. `--harddrive ../build/autotest` runs an
`ATARI_MD_AUTOTEST` build instead; `--headless --frames N --screenshot
out.png --log out.log` runs unattended, and `-O dump=DIR` writes the
firmware's own view of each frame as `mdNNNNN.ppm`.

Changes meant to speed up the game logic can be checked with
`ATARI_LOGIC_CHECK=4` builds: they start a game on a virtual clock (4 tics
a frame, however fast the build is), follow a fixed script of controls in
god mode and write a hash of the game state after every frame to
`LOGIC.TXT` next to the program. Two builds whose game logic behaves the
same write the same file, on any machine or emulated speed.

```sh
make st ATARI_LOGIC_CHECK=4 BUILDDIR=build/check OBJDIR=obj/check
cd sidecart && emu/emumd/tools/mdfw run --headless --frames 13000 \
    --no-user-config --harddrive ../build/check
cmp ../build/check/LOGIC.TXT /path/to/reference/LOGIC.TXT
```

`tests/emu` builds the firmware for the host (`libmdemu`) with the Pico
SDK, flash and FatFs replaced, plus a patch that plugs it into Hatari's
cartridge port.

```sh
make -C sidecart emu        # libmdemu + self-tests (WAD from tmp/ROTT)
make -C sidecart hatari     # Hatari 2.6.1 with the emulated cartridge
```

`tests/emu/run-hatari.sh` runs a program unattended and records the
screen. For ROTT, build it with `ATARI_MD_AUTOTEST=8` (it starts a game
and turns a fixed step each frame) so runs with and without the
Multi-device show the same views; it saves `SHOT008.PI1` ... `SHOT064.PI1`
and `MDDEBUG.TXT` (both palettes and the firmware's status block) next
to itself. `tests/emu/pi1topng.py` converts the shots. In the emulator the
Multi-device is infinitely fast.

```sh
make st ATARI_MD_AUTOTEST=8 ATARI_SHOW_FPS=1 \
    BUILDDIR=build/autotest OBJDIR=obj/autotest
mkdir -p /tmp/run/sd/rott && ln -s $PWD/tmp/ROTT/HUNTBGIN.WAD /tmp/run/sd/rott/
sidecart/tests/emu/run-hatari.sh <hatari> <tos.img> build/autotest /tmp/run 8000 md
```

## Licence

The firmware in `sidecart/` is GPL-3.0-or-later (see `LICENSE`); it
includes code from md-doom, atarist-stdoom and the SidecarTridge
microfirmware templates. The renderer in `rp/src/rott/`, `md_video.c` and
the shared protocol header are GPL-2.0-or-later, like Rise of the Triad.
