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

## Rise of the Triad for Atari TT (ROTTTT?)

The Atari TT version of ROTT that uses a small number of optimisations to maintain ROTT's original look and feel using the MiNTLib SDL library.

Fun fact: this was actually my first attempt at porting ROTT to Atari ST by using a version of SDL I modified to support ST low resolution, but while it still technically runs on any ST compatible computer, and looks great in 16 shades of grey in ST low res, it's sadly too slow to realistically be playable on a regular ST, STE or even Mega STE. So I tried it in TT mode on [Hatari](https://hatari-emu.org). It was amazing and ROTTTT was born!

Music and sound are still very much WIP, so I recommend switching both off in `SOUND.ROT`.

## Screenshots

<img width="638" height="397" alt="image" src="https://github.com/user-attachments/assets/1558c670-be05-427a-b1ce-1ee767a4870e" />

<img width="638" height="397" alt="image" src="https://github.com/user-attachments/assets/17067577-e151-4d6d-a9c6-69a1ef9d9837" />

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

The build process outputs `build/ROTT_TT.TOS`, which will run on any Atari ST compatible computer, but is best experienced on an Atari TT.

## Credits

- Forked from [Amiga port](https://github.com/lantus/ROTT)

## License

This software is distributed in source code format and is licensed under the
terms of the GNU General Public License. A copy of this license is included
with the software in the file COPYING.

This is a completely unofficial port and is not supported by 3D Realms, Apogee, or the porters.
