# STDL suggestions from the ROTT port

Things the native ROTT build (`ROTT.TOS`) found while using STDL's
SDL2 add-on conversion for its 256-colour TT and Falcon screen, and
while doing its own speed work, that STDL could do better and that
other ports would benefit from. All timings are Hatari's (EmuMD's fork,
cycle-exact, `--data-cache on`), from `tools/bench` in this repository,
November 2026. No real TT or Falcon has run them.

Status (STDL v1.33.0, which ROTT now pins): 2, 3, 4 and 6 are done; 1
is open; 5 did not apply. ROTT converts through `STDL_SDL2_ConvertBox`
and `STDL_SDL2_ConvertBoxPairs` now, but keeps its own arithmetic
wrappers (7, below).

## 1. Narrow boxes in the 8-plane conversion

**Open.**

`stdl_sdl2_c2p` leaves its loop twice a row (the row's last group's
stores go out in the next row's first). That's right for wide boxes,
but a box one to three groups wide pays that per-row cost on almost
every group.

ROTT tried converting the 3D view as runs of groups, each its own box
(pairs where it could, plain where it couldn't):

- Runs of single groups, a box each: no gain over converting the
  whole view plainly (TT, 4MB: 101.3 to 101.6ms a frame).
- A row at a time, a call each: slower than the plain whole box.
- Only runs of 4 groups or more taken separately, inside bands of
  rows: the conversion went from 20.5 to 18.3ms on a TT with 2MB
  + 4MB. Converting every row as pairs (wrong pixels, timing only)
  would have been 16.7ms, so about half the possible gain was lost
  to box shape.

Suggestions:

- **A mixed conversion in one call:** for each row, or each band of
  rows, a bitmask of the groups to convert as pairs and the rest
  plain, chosen inside the loop. A program with a mostly-paired
  picture (any low-detail renderer, ADOOM-style) would then pay one
  call and one row loop, whatever the mix.
- **A list of boxes in one call**, sharing the set-up (the 32-byte
  frame, the eleven registers, the constants), for programs that
  convert many small boxes a frame.

## 2. The pair conversion is internal

**Done in v1.33.0:** `STDL_SDL2_ConvertBox` and
`STDL_SDL2_ConvertBoxPairs` (`<SDL2/SDL_stdl_c2p.h>`, no SDL.h needed),
one and two lines a row, linking `c2p.o` and `c2pp.o` alone. ROTT's
`atari_c2pp.c` is gone.

**Correction:** "no faster a row at a time" was the call overhead in
ROTT's frame, not the pairs. STDL measured pairs still saving about 15%
called a row at a time (TT 41.5 against 35.4ms a frame): a call costs
about 40us on a TT and 80 on a Falcon, against about 1.1us saved a
group as pairs.

`c2ppair.c` and `C2P_PPAIR` (`c2p_asm.h`) are only reachable through
`addons/sdl2/src/c2p.h`. ROTT includes that header by path and builds
its own copy (`rott/atari_c2pp.c`: `__asm__(C2P_PPAIR("atari_c2pp",
""))`).

Measured value of pairs:
- 56 instructions a group of 16 against 74;
- in a whole box, the view's conversion on a TT 2+4MB 20.5 to 16.7ms
  (the timing-only test above).

Suggestions:

- A public, documented entry for the pair conversion, box form (with
  a height), for both one-line and two-line (TT) rows.
- Note in `docs/sdl2.md` that it pays only in whole boxes: called a
  row at a time it measured no faster than the plain conversion.

## 3. The `_cache0` label shows up in profiles as a function

**Done in v1.33.0:** the loop labels are out of the symbol table (only
absolute `_cachebytes` sizes remain).

The loop's entry label is a global symbol (`_<name>_cache0`), so
Hatari's profiler counts every pass back through the loop as a call.
ROTT's profile showed `_stdl_sdl2_c2p_plain2_cache0` called 215,134
times in 64 frames (once per 16-pixel group), which at first read as
a call per group. If `sdl2check` only needs the two labels for `nm`,
local `.L` labels plus a size check another way, or a note in the
docs, would avoid that misreading.

## 4. A 68020 path for libgcc's arithmetic in 68000 programs

**Done in v1.33.0:** `libstdl-arith020.o` and `arith020.mk`'s
`STDL_ARITH020`. ROTT tried it and kept its own: see 7.

A 68000 program on a TT or Falcon still calls libgcc for every 32-bit
multiply and divide. ROTT wraps them at the link
(`-Wl,--wrap=__mulsi3` and `__udivsi3`, `__divsi3`, `__umodsi3`,
`__modsi3`, `__muldi3`, `__divdi3`; see `rott/atari_cpu.S`). With the
`_CPU` cookie at 20 or more, each wrapper uses the 68020's own
`muls.l`/`divu.l`, with the 64-bit forms on a 68020–68040 but not
the 68060. Otherwise it goes on to libgcc's. Each gives libgcc's
answer to the bit.

- Before ROTT's other work, libgcc arithmetic was 31ms of a 227ms TT
  frame.
- The same sources compiled `-m68030` only as an experiment: 190ms
  against 227.

Suggestion: ship this as an optional object (`stdl_cpuwrap.o` or
similar) with the link flags documented. Every 68000 STDL program
that runs on a TT or Falcon would get it for nothing. ROTT's version,
with its tests (`tools/bench/fixtest.sh` and `fixtest.c`, comparing
against the C on random and edge-case inputs on Hatari's TT and STE),
could be the starting point.

## 5. Shift-free table indexes for 16-colour conversion

**Did not apply:** STDL's ST conversion already runs at about 40 cycles
a pixel, as fast as ROTT's after this change.

ROTT's ST Low conversion (its own, `rott/atari_c2p.c`, not STDL's)
looks colours up in a dithering table. Laying the table out one colour
every 256 bytes changes how the index is made:

- Masking a long of chunky pixels with `and.l #$FF00FF00` makes two
  table indexes directly, with no shift.
- `d8(An,Xn.w)` sign-extends the index, so the table is addressed from
  its middle: colour c is at `base + 32768 + (short)(c << 8)`.

This removes the `lsl`/`lsr` from the inner loops: an 8MHz ST frame
went from 1,033 to 1,005ms. It costs 64K of table.

Suggestion: if any STDL table-driven conversion (the SDL2 add-on's
16-colour ST screen, the indexed blits) builds its index with shifts,
the same layout may pay there, where the memory allows.

## 6. Pair conversion for 16 colours

**Done in v1.33.0:** `STDL_SDL2_RenderCopyPairs` on an ST's 16-colour
screen in the 68000 build (`stpair.c`).

On an ST, a row of identical pixel pairs converts through the 2x table
at about 18.5 cycles a pixel against 41. ROTT gets this exactly by
tracking where anything was drawn one column wide. For a low-detail
renderer that took an 8MHz frame from 1,174 to 1,060ms.

Suggestion: STDL's own 16-colour paths for SDL-style chunky screens
could offer a pairs mode, as the 8-plane conversion does.

## 7. `libstdl-arith020.o` on a 68000

Linked in place of ROTT's wrappers (ROTT's FixedMul and FixedScale
kept), the game was exact but slower on every machine (ms a frame):
8MHz ST 986.4 to 987.5, TT 101.4 to 101.5, TT 2+4MB 77.7 to 77.9.
The ST's cost is the 68000 path: STDL's test, branch and `jmp` (41
cycles) against ROTT's `tst.b` and a `beq` straight to `__real_...`
(26), its flag set in `main` from the `_CPU` cookie. About 630 calls
an 8MHz ST frame, libgcc's own `__divsi3` -> `__udivsi3` included.

Suggestion: the 68000 path as a `beq.w` straight to `__real_...`, in
reach when the object is linked just before the libraries, as ROTT's
is (about 1ms of an 8MHz ST's frame here).
