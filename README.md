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

## Rise of the Triad for the web (WebROTT?)

After creating the Atari ST and TT versions of ROTT, I thought it was only fair that I should create a version for those of you that, for whatever crazy reason, don't own any of Atari's late-80s or early-90s hardware.

Welcome to WebROTT: all of the original features, no installation required.

[Click here to try it now!](https://labs.neilrackett.com/rott)

## Screenshots

<img width="638" height="397" alt="image" src="https://github.com/user-attachments/assets/1558c670-be05-427a-b1ce-1ee767a4870e" />

<img width="638" height="397" alt="image" src="https://github.com/user-attachments/assets/17067577-e151-4d6d-a9c6-69a1ef9d9837" />

## Installation

Click on the link above. Play the game. That's it.

## Build

The WebAssembly version of ROTT is built using [Emscripten](https://emscripten.org/).

- Install the shareware version of ROTT for DOS using DOSbox, or [download the files from Internet.org](https://archive.org/details/rott_shareware)
- Copy the files into a `build` folder in the root of this project
- Run `make`

All of the files you need to deploy WebROTT will be in the `web/build` folder.

## License

This software is distributed in source code format and is licensed under the
terms of the GNU General Public License. A copy of this license is included
with the software in the file COPYING.

This is a completely unofficial port and is not supported by 3D Realms, Apogee, or the porters.
