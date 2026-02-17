# Rise of the Triad (ROTT) for Atari ST, TT & Falcon

Ported by [Neil Rackett](https://x.com/neilrackett)

## Introduction

What better way to celebrate the 30th-ish anniversary of ROTT than to port it to a hardware platform currently celebrating its 40th: _Welcome to Rise of the Triad for Atari ST, TT & Falcon!_

This repository contains 2 versions of ROTT:

| Branch | Description                                                                                     | Target                        |
| ------ | ----------------------------------------------------------------------------------------------- | ----------------------------- |
| `sdl`  | SDL based port aiming to be as close to original ROTT as possible on all ST-compatible hardware | ST, STE, Mega STE, TT, Falcon |
| `c2p`  | Aggressively optimised direct-to-hardware port with C2P rendering for best performance on ST    | ST, STE, Mega STE             |

All builds:

- Require 4MB RAM
- Need you to install or download the DOS version of [ROTT shareware ("The Hunt Begins")](https://archive.org/details/rott_shareware)
- Support keyboard and mouse input

Stable builds are avilable on the [releases page](https://github.com/neilrackett/atarist-rott/releases).

Enjoy!

## Rise of the Triad (SDL)

<img width="320" height="200" alt="image" src="https://github.com/user-attachments/assets/1558c670-be05-427a-b1ce-1ee767a4870e" /> <img width="320" height="200" alt="image" src="https://github.com/user-attachments/assets/17067577-e151-4d6d-a9c6-69a1ef9d9837" />

This port uses the Simple DirectMedia Layer (SDL) framework, patched to support Atari ST low-res, to bring ROTT to the Atari ST, TT & Falcon with a minimal number of tweaks and performance optimisations to retain as much of the game's original look and feel as possible, including intro videos and menu animations. TT screenshots shown.

| Model    | Colours        | Music | SFX | Typical FPS |
| -------- | -------------- | ----- | --- | ----------- |
| ST       | 16 (greyscale) | ✅    | ❌  | 1-2         |
| STE      | 16 (greyscale) | ✅    | ✅  | 1-2         |
| Mega STE | 16 (greyscale) | ✅    | ✅  | 2-3         |
| TT       | 256            | ✅    | ✅  | 12-15       |
| Falcon   | 256            | ✅    | ✅  | 10-12       |

FPS based on running ROTT with automatic detail selection enabled. Music and sound are still WIP, so I recommend switching both off in `SOUND.ROT` for now.

Screenshots from Atari TT & Falcon.

## Installation

- Install the shareware version of ROTT for DOS using DOSbox, or [download the files from Internet.org](https://archive.org/details/rott_shareware)
- Copy the installation folder to your Atari's hard disk
- Copy `ROTT_SDL.TOS` and/or `ROTT_030.TOS` to the the same folder (the 030 build runs 20-30% faster on TT/Falcon)
- Run `ROTT_SDL.TOS` or `ROTT_030.TOS`
- Enjoy!

Edit `CONFIG.ROT` to adjust rendering and performance settings.

## Build

The quickest way to build ROTT for yourself is to install [atarist-toolkit-docker](https://github.com/sidecartridge/atarist-toolkit-docker) then run:

```bash
stcmd make
```

The build process outputs `build/sdl/ROTT_SDL.TOS`, which will run on any Atari ST compatible computer, and `build/sdl/ROTT_030.TOS` which is optimised for 68030 CPU with 68882 FPU (TT & Falcon).

## License

This software is distributed in source code format and is licensed under the
terms of the GNU General Public License. A copy of this license is included
with the software in the file COPYING.

This is a completely unofficial port and is not supported by 3D Realms, Apogee, or the porters.
