# Rise of the Triad

Atari ST port by [Neil Rackett](https://x.com/neilrackett)

## Introduction

What better way to celebrate the 30th-ish anniversary of ROTT than to port it to a hardware platform currently celebrating its 40th: Welcome to _Rise of the Triad for Atari ST_.

- This port is optimised for the Atari Mega STE, but will run on any ST.
- Sound effects are STE-only
- It's still experimental, but enjoy!

## Installation

- Install the shareware version of ROTT for DOS using DOSbox, or [download the files from Internet.org](https://archive.org/details/rott_shareware)
- Copy the latest release of `ROTT.TOS` to the installation folder
- Run `ROTT.TOS`
- Enjoy!

## Build

You can build the shareware version of ROTT for Atari ST, "The Hunt Begins", using [atarist-toolkit-docker](https://github.com/sidecartridge/atarist-toolkit-docker) and have the option of building the standard version that uses a dynamic 16-colour palette with bayer dithering or _ROTT Noir_ that renders using a fixed greyscale palette:

```bash
stcmd make
stcmd make ATARI_NOIR=1
stcmd make ATARI_NOIR=1 ATARI_NOIR_DITHERING=1
```

You can compile the commercial versions of ROTT using:

```bash
stcmd make rott-darkwar
stcmd make rott-rottcd
stcmd make rott-rottsite
```

## Credits

- Forked from [Amiga port](https://github.com/lantus/ROTT)

## License

This software is distributed in source code format and is licensed under the
terms of the GNU General Public License. A copy of this license is included
with the software in the file COPYING.

This is a completely unofficial port and is not supported by 3D Realms, Apogee, or the porters.
