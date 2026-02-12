# Rise of the Triad

Atari ST and WebAssembly (WASM) ports by [Neil Rackett](https://x.com/neilrackett)

## Introduction

What better way to celebrate the 30th-ish anniversary of ROTT than to port it to a hardware platform currently celebrating its 40th: Welcome to _Rise of the Triad for Atari ST_ (and TT and WebAssembly).

| Branch     | Name              | Description                                                          | Optimised for  | Compatibile with              | Compiler            |
| ---------- | ----------------- | -------------------------------------------------------------------- | -------------- | ----------------------------- | ------------------- |
| `atari-st` | ROTT for Atari ST | C2P rendering, 16 colour and Noir (greyscale) versions               | Atari Mega STE | ST, STE, Mega STE, TT, Falcon | m68k-atari-mint-gcc |
| `atari-tt` | ROTT for Atari TT | SDL rendering, 16 colour (greyscale) on ST, 256 colours on TT/Falcon | Atari TT       | ST, STE, Mega STE, TT, Falcon | m68k-atari-mint-gcc |
| `wasm`     | ROTT for the web  | Web version using WebAssembly                                        | Web            | Any modern browser            | emcc                |

All builds are experimental.

Enjoy!

## Rise of the Triad for Atari ST (STROTT?)

This port is a highly optimised version of ROTT using C2P rendering to make it run on any Atari ST compatible computer or [Hatari](https://hatari-emu.org), with the primary goal of making it (just about) playable on an Atari Mega STE in 16Mhz mode.

To achieve this, optimisations include:

- Static intro screens
- Automatic 2x or 4x game area zoom based on selected view size
- Event-driven menus
- Changes to keyboard input handling
- Use blitter chip for HUD, if available
- Skip precache, fades, fizzle, advanced lighting
- Low-memory mode always on

## Installation

- Install the shareware version of ROTT for DOS using DOSbox, or [download the files from Internet.org](https://archive.org/details/rott_shareware)
- Copy the latest release of `ROTT_ST.TOS` to the installation folder
- Run `ROTT_ST.TOS`
- Enjoy!

## Build

You can build the shareware version of ROTT for Atari ST, "The Hunt Begins", using [atarist-toolkit-docker](https://github.com/sidecartridge/atarist-toolkit-docker) and have the option of building the standard version that uses a dynamic 16-colour palette with bayer dithering or _ROTT Noir_ that renders using a fixed greyscale palette:

```bash
stcmd make
stcmd make ATARI_NOIR=1
stcmd make ATARI_NOIR=1 ATARI_NOIR_DITHERING=1
```

The build process outputs `build/ROTT_ST.TOS`, which will run on Hatari or any Atari ST compatible computer in ST low resolution.

You can compile the commercial versions of ROTT using:

```bash
stcmd make rott-darkwar
stcmd make rott-rottcd
stcmd make rott-rottsite
```

These builds output `build/ROTT_STD.TOS`, `build/ROTT_STC.TOS` and `build/ROTT_STS.TOS`, respectively.

## Credits

- Forked from [Amiga port](https://github.com/lantus/ROTT)

## License

This software is distributed in source code format and is licensed under the
terms of the GNU General Public License. A copy of this license is included
with the software in the file COPYING.

This is a completely unofficial port and is not supported by 3D Realms, Apogee, or the porters.
