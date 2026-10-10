# Rise of the Triad (ROTT) for Atari ST, TT & Falcon

Ported by [Neil Rackett](https://x.com/neilrackett)

## Introduction

What better way to celebrate the 30th-ish anniversary of ROTT than to port it to a hardware platform currently celebrating its 40th: _Welcome to Rise of the Triad for Atari ST, TT & Falcon!_

This repository contains 3 versions of ROTT:

| Build          | Description                                                                                            | Target                        |
| -------------- | ------------------------------------------------------------------------------------------------------ | ----------------------------- |
| `ROTT_ST.TOS`  | Aggressively optimised native Atari ST port & optional ROTT Accelerator for SidecarTridge Multi-device | ST, STE, Mega STE             |
| `ROTT_SDL.TOS` | SDL based port aiming to be as close to original ROTT as possible on all ST-compatible hardware        | ST, STE, Mega STE, TT, Falcon |
| `ROTT_030.TOS` | The SDL port, optimised for a 68030 CPU with 68882 FPU                                                 | TT, Falcon                    |

`ROTT_ST.TOS` can also hand its 3D view over to a [SidecarTridge Multi-device](https://sidecartridge.com), if you have one: see [ROTT Accelerator](#rott-accelerator-sidecartridge-multi-device) below.

All builds:

- Require 4MB RAM
- Need you to install or download the DOS version of [ROTT shareware ("The Hunt Begins")](https://archive.org/details/rott_shareware)
- Support keyboard and mouse controls

Stable builds are avilable on the [releases page](https://github.com/neilrackett/atarist-rott/releases).

Enjoy!

## Rise of the Triad (Atari ST)

<img width="320" height="200" alt="image" src="https://github.com/user-attachments/assets/f1d21051-bd68-41bb-a2a0-a9e23586cc83" /> <img width="320" height="200" alt="image" src="https://github.com/user-attachments/assets/49880232-74c0-4317-93aa-d295792db07f" />

This port talks directly to the ST's hardware alongside C2P rendering and aggressive code optimisations with the goal of making ROTT as playable as possible on a regular Atari ST, STE or Mega STE (automatically switched to 16Mhz mode).

To achieve this, optimisations include:

- 16 colours with dithering
- Static intro screens
- Automatic 2x or 4x game area zoom based on selected view size
- Event-driven menus
- Optimised keyboard input handling
- Blitter chip HUD updates (if available)
- Skip precache, fades, fizzle, advanced lighting
- Low-memory mode always on
- Using lower-precision numbers for internal calculations
- And more!

| Model    | Music | SFX | Typical FPS |
| -------- | ----- | --- | ----------- |
| ST       | ✅    | ❌  | 2-3         |
| STE      | ✅    | ✅  | 2-3         |
| Mega STE | ✅    | ✅  | 3-5         |

So, in terms of raw FPS it's 2-3x faster than the SDL build, which is great to see, but not quite as playable as I would like. Please feel free to fork it and send me a PR if you think you can squeeze a few more FPS out of it!

FPS based on running ROTT with automatic detail selection enabled.

Music plays ROTT's MIDI songs on the ST's own sound chip, so it works on any ST. Set `MusicMode` to `0` in `SOUND.ROT` to turn it off, or `MusicVolume` (0-255) to change its volume.

Sound effects use the STE's DMA sound, so they need an STE or Mega STE (an ST just plays without them). Set `FXMode` to `0` in `SOUND.ROT` to turn them off. `NumVoices` sets how many play at once, from 1 to 8; each one costs CPU time while it plays (about 5% of a Mega STE for one), and nothing when it's quiet. The default, `0`, plays one at a time, or four with the [ROTT Accelerator](#rott-accelerator-sidecartridge-multi-device).

## Rise of the Triad (SDL)

<img width="320" height="200" alt="image" src="https://github.com/user-attachments/assets/1558c670-be05-427a-b1ce-1ee767a4870e" /> <img width="320" height="200" alt="image" src="https://github.com/user-attachments/assets/17067577-e151-4d6d-a9c6-69a1ef9d9837" />

`ROTT_SDL.TOS` and `ROTT_030.TOS` use the Simple DirectMedia Layer (SDL) framework, patched to support Atari ST low-res, to bring ROTT to the Atari ST, TT & Falcon with a minimal number of tweaks and performance optimisations to retain as much of the game's original look and feel as possible, including intro videos and menu animations. TT screenshots shown.

| Model    | Colours        | Music | SFX | Typical FPS |
| -------- | -------------- | ----- | --- | ----------- |
| ST       | 16 (greyscale) | ✅    | ❌  | 1-2         |
| STE      | 16 (greyscale) | ✅    | ✅  | 1-2         |
| Mega STE | 16 (greyscale) | ✅    | ✅  | 2-3         |
| TT       | 256            | ✅    | ✅  | 12-15       |
| Falcon   | 256            | ✅    | ✅  | 10-12       |

FPS based on running ROTT with automatic detail selection enabled. Music and sound are still WIP, so I recommend switching both off in `SDLSOUND.ROT` for now.

The SDL versions keep their settings in `SDLCONF.ROT` and `SDLSOUND.ROT`, so they can share a folder with `ROTT_ST.TOS`, whose `CONFIG.ROT` and `SOUND.ROT` are tuned for a very different renderer. Edit `SDLCONF.ROT` to adjust rendering and performance settings; `rott/sdlconf_st.rot` is a starting point for an ST.

## ROTT Accelerator (SidecarTridge Multi-device)

If you have a [SidecarTridge Multi-device](https://store.sidecartridge.com/products/sidecartridge-multi-device), the ROTT Accelerator can take over the heavy lifting: your ST keeps running the game, including the HUD, menus, sound and input, while the Multi-device's RP2040 renders the 3D view (walls, floors, ceilings, skies and sprites) using ROTT's own renderer and hands each finished frame back to your ST.

As well as taking the load off the ST, it brings back some of what the ST version had to cut:

- Full lighting, including darker areas and light diminishing
- Full resolution at every view size, rather than a 2x or 4x zoom, up to the full width between the status bars
- On-screen messages

`ROTT_ST.TOS` looks for the ROTT Accelerator when it starts and falls back to the ST renderer if it isn't there, so there's still only one version to install.

See the [ROTT Accelerator readme](sidecart/README.md) for installation, testing and build instructions.

## Gamepads (Xpad)

`ROTT_ST.TOS` also plays with a gamepad through [Xpad](https://downloads.neilrackett.com/atarist-rott), with analogue turning and movement on the left stick. It needs an Xpad driver that reads the pad's hardware directly, such as the STE enhanced port driver or a cartridge-port adapter: the Xpad joystick and keyboard drivers cannot work here, because this version reads the keyboard itself.

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

- Install the shareware version of ROTT for DOS using DOSbox, or [download the files from Internet.org](https://archive.org/details/rott_shareware)
- Copy the installation folder to your Atari's hard disk
- Copy `ROTT_ST.TOS`, `ROTT_SDL.TOS` and/or `ROTT_030.TOS` to the the same folder (the 030 build runs 20-30% faster on TT/Falcon)
- Optionally, install the ROTT Accelerator on your SidecarTridge Multi-device (see the [ROTT Accelerator readme](sidecart/README.md))
- Run `ROTT_ST.TOS`, `ROTT_SDL.TOS` or `ROTT_030.TOS`
- Enjoy!

## Build

The quickest way to build ROTT for yourself is to install [atarist-toolkit-docker](https://github.com/sidecartridge/atarist-toolkit-docker), fetch the submodules (`git submodule update --init`), then run:

```bash
make             # the three games
make st          # ROTT_ST.TOS
make sdl         # ROTT_SDL.TOS
make sdl-030     # ROTT_030.TOS
make sidecart    # the ROTT Accelerator firmware
make shareware   # HUNTBGIN.zip: the shareware data the games use, from tmp/ROTT
```

The games are built through `stcmd`, so you can run these from your own shell (or run `stcmd make st` and so on yourself). The firmware needs the Pico toolchain too: see the [ROTT Accelerator readme](sidecart/README.md). Everything goes to `dist/`, along with the config files and, if you put the shareware files in `tmp/ROTT`, the game data, so `dist/` is ready to run in an emulator or copy to your Atari.

There's loads of build settings you can try too, just take a look at `Makefile`, including the ability to build ROTT Noir, a greyscale version that can be built with or without dithering:

```bash
make st ATARI_NOIR=1
make st ATARI_NOIR=1 ATARI_NOIR_DITHERING=1
```

`ROTT_ST.TOS` is optimised for Atari ST computers, but will run on any Atari ST compatible hardware.

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
