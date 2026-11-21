# Rise of the Triad (ROTT) for Atari ST, TT & Falcon

Ported by [Neil Rackett](https://x.com/neilrackett)

## Introduction

What better way to celebrate the 30th-ish anniversary of ROTT than to port it to a hardware platform currently celebrating its 40th: _Welcome to Rise of the Triad for Atari ST, TT & Falcon!_

This repository contains 2 versions of ROTT:

| Build          | Description                                                                                                           | Target                        |
| -------------- | --------------------------------------------------------------------------------------------------------------------- | ----------------------------- |
| `ROTT.TOS`     | Aggressively optimised native port for the whole ST family & optional ROTT Accelerator for SidecarTridge Multi-device | ST, STE, Mega STE, TT, Falcon |
| `ROTT_SDL.TOS` | SDL based port for 68030 machines, aiming to be as close to original ROTT as possible                                 | TT, Falcon                    |

If you're not sure which to use, use `ROTT.TOS`. If you have `ROTT_ST.TOS` or `ROTT_030.TOS` from an earlier release, delete them: `ROTT_ST.TOS` is now `ROTT.TOS`, and `ROTT_SDL.TOS` replaces `ROTT_030.TOS` (it no longer needs an FPU, and no longer runs on an ST, where it was too slow to play).

`ROTT.TOS` can also hand its 3D view over to a [SidecarTridge Multi-device](https://sidecartridge.com), if you have one: see [ROTT Accelerator](#rott-accelerator-sidecartridge-multi-device) below.

All builds:

- Require 4MB RAM
- Need the DOS version of ROTT shareware ("The Hunt Begins"), which is on the releases page as [`HUNTBGIN.zip`](https://github.com/neilrackett/atarist-rott/releases/download/latest/HUNTBGIN.zip)
- Support keyboard and mouse controls

Stable builds are avilable on the [releases page](https://github.com/neilrackett/atarist-rott/releases).

Enjoy!

## Rise of the Triad (Atari ST family)

<img width="320" height="200" alt="image" src="https://github.com/user-attachments/assets/f1d21051-bd68-41bb-a2a0-a9e23586cc83" /> <img width="320" height="200" alt="image" src="https://github.com/user-attachments/assets/49880232-74c0-4317-93aa-d295792db07f" />

This port talks directly to the hardware alongside C2P rendering and aggressive code optimisations with the goal of making ROTT as playable as possible on every machine in the ST family: a regular Atari ST, STE or Mega STE (automatically switched to 16MHz mode), a TT or a Falcon. It's one program for all of them: on a TT or a Falcon it switches to their 256-colour modes and their 68030's faster maths by itself.

To achieve this, optimisations include:

- 16 colours with dithering on an ST, STE or Mega STE, chosen for what the game actually draws (256 colours on a TT or a Falcon)
- Inner drawing loops and maths hand-written in 68000 assembler, with 68030 versions picked at startup on a TT or a Falcon
- Low detail (the default) draws the sky, floors, ceilings and railings at half width, as it does the walls
- Static intro screens
- Optional 2x or 4x game area zoom for small view sizes (`ViewZoom 1` in `CONFIG.ROT`)
- Event-driven menus
- Optimised keyboard input handling
- Blitter chip HUD updates (if available)
- Skip precache, fades, fizzle, advanced lighting
- Low-memory mode always on
- Using lower-precision numbers for internal calculations
- Distant moving walls, elevator disks and idle enemies out of sight are updated less often
- And more!

| Model    | Music | SFX | Native FPS | ROTT Accelerator FPS |
| -------- | ----- | --- | ---------- | -------------------- |
| ST       | ✅    | ❌  | 2          | 17                   |
| STE      | ✅    | ✅  | 2          | 17                   |
| Mega STE | ✅    | ✅  | 3          | 30                   |

On its own it runs up to twice as fast as the SDL build, though the game slows down to keep up. With the ROTT Accelerator it's up to 17 times as fast and the game runs at full speed, with a full-size view at high detail.

Native FPS was recorded at the smallest view size (80x48, zoomed 4x with `ViewZoom 1`) and low detail, and ROTT Accelerator FPS at the default view size (320x168) and high detail.

Music plays ROTT's MIDI songs on the ST's own sound chip, so it works on any ST. Set `MusicMode` to `0` in `SOUND.ROT` to turn it off, or `MusicVolume` (0-255) to change its volume. With the [ROTT Accelerator](#rott-accelerator-sidecartridge-multi-device) (firmware v1.4.0-beta.4 or later), the Multi-device plays the songs and your ST just copies the notes to its sound chip, which gives an 8MHz ST about 7% of its time back.

On a TT or a Falcon with a colour monitor, `ROTT.TOS` shows the game's own 256 colours (TT Low, or a Falcon's 8-bit mode on VGA, RGB or a TV) in place of the 16 and their dither; set `Colours` to `16` in `CONFIG.ROT` for ST Low. `ViewZoom` works there too. TT and Falcon support has only been run in an emulator so far.

Sound effects use the STE's DMA sound, so they need an STE or Mega STE (an ST just plays without them). Set `FXMode` to `0` in `SOUND.ROT` to turn them off. `NumVoices` sets how many play at once, from 1 to 8; each one costs CPU time while it plays (about 5% of a Mega STE for one), and nothing when it's quiet. The default, `0`, plays one at a time, or two with the [ROTT Accelerator](#rott-accelerator-sidecartridge-multi-device). On an 8MHz ST they're mixed at 6258Hz rather than 12517Hz, for half the CPU time: set `FXRate` to `12517` for the full rate.

## Rise of the Triad (SDL)

<img width="320" height="200" alt="image" src="https://github.com/user-attachments/assets/1558c670-be05-427a-b1ce-1ee767a4870e" /> <img width="320" height="200" alt="image" src="https://github.com/user-attachments/assets/17067577-e151-4d6d-a9c6-69a1ef9d9837" />

`ROTT_SDL.TOS` uses the Simple DirectMedia Layer (SDL) framework, through [STDL](https://github.com/neilrackett/atarist-stdl)'s SDL 1.2 add-on, to bring ROTT to the Atari TT & Falcon with a minimal number of tweaks and performance optimisations to retain as much of the game's original look and feel as possible, including intro videos and menu animations. TT screenshots shown.

| Model  | Colours | Music | SFX | Typical FPS |
| ------ | ------- | ----- | --- | ----------- |
| TT     | 256     | ✅    | ✅  | 12          |
| Falcon | 256     | ✅    | ✅  | 10          |

It's 68030 code, so it needs a TT or a Falcon, but not an FPU. FPS based on running ROTT with automatic detail selection enabled. Music and sound effects cost a little of the frame rate: set `MusicMode` and `FXMode` to `0` in `SDLSOUND.ROT` to switch them off.

The SDL version keeps its settings in `SDLCONF.ROT` and `SDLSOUND.ROT`, so it can share a folder with `ROTT.TOS`, whose `CONFIG.ROT` and `SOUND.ROT` are tuned for a very different renderer. Edit `SDLCONF.ROT` to adjust rendering and performance settings.

## ROTT Accelerator (SidecarTridge Multi-device)

If you have a [SidecarTridge Multi-device](https://store.sidecartridge.com/products/sidecartridge-multi-device), the ROTT Accelerator can take over the heavy lifting: your ST keeps running the game, including the HUD, menus, sound effects and input, while the Multi-device's RP2040 renders the 3D view (walls, floors, ceilings, skies and sprites) using ROTT's own renderer and hands each finished frame back to your ST. It plays the music too.

As well as taking the load off the ST, it brings back some of what the ST version had to cut:

- Full lighting, including darker areas and light diminishing
- Full resolution at every view size, rather than a 2x or 4x zoom, up to the full width between the status bars
- On-screen messages
- On a TT or a Falcon, the view in the game's own 256 colours (firmware v1.4.0-beta.8 or later; older firmware keeps ST Low's 16)

`ROTT.TOS` looks for the ROTT Accelerator when it starts and falls back to the ST renderer if it isn't there, so there's still only one version to install. While the Accelerator is rendering, a small yellow lightning bolt shows on the bottom status bar.

It can be installed from the SidecarTridge Multi-device app store or manually: see the [ROTT Accelerator readme](sidecart/README.md) for installation, testing and build instructions.

## Gamepads (Xpad)

`ROTT.TOS` also plays with a gamepad through [Xpad](https://downloads.neilrackett.com/atarist-rott), with analogue turning and movement on the left stick. It needs an Xpad driver that reads the pad's hardware directly, such as the STE enhanced port driver or a cartridge-port adapter: the Xpad joystick and keyboard drivers cannot work here, because this version reads the keyboard itself.

With a driver that can drive the pad's motors, such as [COMpad](https://github.com/neilrackett/pico-compad) on the modem port, the pad rumbles when you fire (a buzz for the guns, a kick for the missile weapons), take damage (harder for bigger hits), are caught in an explosion or shoved by a moving wall, and when you die.

| Pad               | In the game              | In menus |
| ----------------- | ------------------------ | -------- |
| Left stick, D-pad | Turn and move            | Move     |
| A                 | Fire                     | Select   |
| B                 | Use (open, press)        | Back     |
| X                 | Strafe                   |          |
| Y                 | Run                      |          |
| LB, RB            | Previous, next weapon    |          |
| LT, RT            | Strafe left, right       |          |
| Right stick       | Look up and down, strafe |          |
| Left stick click  | Turn around              |          |
| Start             | Menu                     | Back     |
| Select            | Map                      |          |
| Guide             | Pause                    |          |

## Installation

- Download [`HUNTBGIN.zip`](https://github.com/neilrackett/atarist-rott/releases/download/latest/HUNTBGIN.zip), the ROTT 1.3 shareware release (plus a `SOUND.ROT` with music and sound effects on), from the [releases page](https://github.com/neilrackett/atarist-rott/releases) and unzip it into a folder on your Atari's hard disk (or install the [shareware from archive.org](https://archive.org/details/rott_shareware) for DOS using DOSBox and copy the installation folder across)
- Copy `ROTT.TOS` and, on a TT or a Falcon, `ROTT_SDL.TOS` if you want it, to the same folder
- Optionally, install the ROTT Accelerator on your SidecarTridge Multi-device (see the [ROTT Accelerator readme](sidecart/README.md)); `ROTT.TOS` copies the WAD to its SD card for you
- Run `ROTT.TOS` or `ROTT_SDL.TOS`
- Enjoy!

## Build

The quickest way to build ROTT for yourself is to install [atarist-toolkit-docker](https://github.com/sidecartridge/atarist-toolkit-docker), fetch the submodules (`git submodule update --init`, then `git submodule update --init --recursive lib/stdl`, which both games use), then run:

```bash
make             # both games
make st          # ROTT.TOS
make sdl         # ROTT_SDL.TOS
make sidecart    # the ROTT Accelerator firmware
make shareware   # HUNTBGIN.zip: the ROTT 1.3 shareware release, from tmp/ROTT_DOS
```

The games are built through `stcmd`, so you can run these from your own shell (or run `stcmd make st` and so on yourself). The firmware needs the Pico toolchain too: see the [ROTT Accelerator readme](sidecart/README.md). Everything goes to `dist/`, along with the config files and, if you unzip `HUNTBGIN.zip` into `tmp/ROTT`, the game data, so `dist/` is ready to run in an emulator or copy to your Atari.

There's loads of build settings you can try too, just take a look at `Makefile`, including the ability to build ROTT Noir, a greyscale version that can be built with or without dithering:

```bash
make st ATARI_NOIR=1
make st ATARI_NOIR=1 ATARI_NOIR_DITHERING=1
```

`ROTT.TOS` is one 68000 program for the whole ST family: on a TT or a Falcon it picks its 68030 code and 256-colour screen when it starts.

SidecarTridge Multi-device support is included by default. To build without it:

```bash
make st ATARI_MD_RENDER=0
```

You can compile the commercial versions of ROTT using:

```bash
stcmd make rott-darkwar
stcmd make rott-rottcd
stcmd make rott-rottsite
```

## License

This software is distributed in source code format and is licensed under the
terms of the GNU General Public License. A copy of this license is included
with the software in the file COPYING.

Controller input via Xpad, Copyright (c) 2026 Neil Rackett, BSD-2-Clause. See XPAD.TXT.

This is a completely unofficial port and is not supported by 3D Realms, Apogee, or the porters.
