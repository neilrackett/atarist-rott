# Rise of the Triad (ROTT) for Atari ST, TT & Falcon

Ported by [Neil Rackett](https://x.com/neilrackett)

## Introduction

What better way to celebrate the 30th-ish anniversary of ROTT than to port it to a hardware platform currently celebrating its 40th: _Welcome to Rise of the Triad for Atari ST, TT & Falcon!_

This repository contains 2 versions of ROTT:

| Branch    | Description                                                                                     | Target                        |
| --------- | ----------------------------------------------------------------------------------------------- | ----------------------------- |
| `sdl`     | SDL based port aiming to be as close to original ROTT as possible on all ST-compatible hardware | ST, STE, Mega STE, TT, Falcon |
| `atarist` | Aggressively optimised native Atari ST port                                                     | ST, STE, Mega STE             |

All builds:

- Require 4MB RAM
- Need you to install or download the DOS version of [ROTT shareware ("The Hunt Begins")](https://archive.org/details/rott_shareware)
- Support keyboard and mouse input

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

## Installation

- Install the shareware version of ROTT for DOS using DOSbox, or [download the files from Internet.org](https://archive.org/details/rott_shareware)
- Copy the installation folder to your Atari's hard disk
- Copy `ROTT_ST.TOS` to the the same folder
- Run `ROTT_ST.TOS`
- Enjoy!

## Build

The quickest way to build ROTT for yourself is to install [atarist-toolkit-docker](https://github.com/sidecartridge/atarist-toolkit-docker) then run:

```bash
stcmd make
```

There's loads of build settings you can try too, just take a look at `Makefile`, including the ability to build ROTT Noir, a greyscale version that can be built with or without dithering:

```bash
stcmd make ATARI_NOIR=1
stcmd make ATARI_NOIR=1 ATARI_NOIR_DITHERING=1
```

The build process outputs `build/atarist/ROTT_ST.TOS`, which is optimised for Atari ST computers, but will run on any Atari ST compatible hardware.

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

This is a completely unofficial port and is not supported by 3D Realms, Apogee, or the porters.
