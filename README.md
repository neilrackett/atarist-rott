# Rise of the Triad

Atari ST and WebAssembly (WASM) ports by [Neil Rackett](https://x.com/neilrackett)

## Introduction

What better way to celebrate the 30th-ish anniversary of ROTT than to port it to a hardware platform currently celebrating its 40th: Welcome to _Rise of the Triad for Atari ST_ (and TT and WebAssembly).

| Branch     | Description                                                                                                        | Optimised for  | Compatibile with              | Compiler            |
| ---------- | ------------------------------------------------------------------------------------------------------------------ | -------------- | ----------------------------- | ------------------- |
| `atari-st` | ROTT for Atari ST using C2P rendering and direct audio, SFX are STE-only, 16-colours and Noir (greyscale) versions | Atari Mega STE | ST, STE, Mega STE, TT, Falcon | m68k-atari-mint-gcc |
| `atari-tt` | ROTT for Atari TT using SDL: 16 colour (greyscale) on ST or 256 colours on TT/Falcon, sound still WIP              | Atari TT       | ST, STE, Mega STE, TT, Falcon | m68k-atari-mint-gcc |
| `wasm`     | ROTT for the web                                                                                                   | WASM           | Web browser                   | emcc                |

All builds are experimental.

Enjoy!

## Installation

- Install the shareware version of ROTT for DOS using DOSbox, or [download the files from Internet.org](https://archive.org/details/rott_shareware)
- Copy `ROTT_TT.TOS` to the installation folder
- Run `ROTT_TT.TOS`
- Enjoy!

## Build

The Atari TT build uses the SDL library for graphics and sound, and you can build the shareware version of ROTT ("The Hunt Begins") for Atari TT using [atarist-toolkit-docker](https://github.com/sidecartridge/atarist-toolkit-docker):

```bash
stcmd make
```

The build process outputs `build/ROTT_TT.TOS`, which will run on Hatari or any Atari ST compatible computer. However, while it looks great on an ST, it's too slow to be very playable.

Music and sound in this build is still very much WIP, so we recommend switching both off in `SOUND.ROT`.

## Credits

- Forked from [Amiga port](https://github.com/lantus/ROTT)

## License

This software is distributed in source code format and is licensed under the
terms of the GNU General Public License. A copy of this license is included
with the software in the file COPYING.

This is a completely unofficial port and is not supported by 3D Realms, Apogee, or the porters.
