# AGENTS.md

This file is for future contributors and coding agents working in this repository.
It focuses on the current Atari Mega STE SDL branch state and practical workflow.

## 1) Project Intent

- Project: SDL-based port of Rise of the Triad (1994), adapted here for Atari Mega STE.
- Primary target executable: `build/ROTT.TOS`.
- Primary runtime target: Atari Mega STE class machine (real hardware + Hatari).
- Baseline working mode: software-rendered SDL path at `320x200`.

## 2) Current Build Contract

- Build command: `stcmd make`
- Compiler: `m68k-atari-mint-gcc`
- Top-level build file: `Makefile`
- Object output: `obj/.../*.o`
- Final binary output: `build/ROTT.TOS`
- Default product flags:
- `SHAREWARE=1`
- `SUPERROTT=0`
- `SITELICENSE=0`

Notes:
- The top-level `Makefile` is authoritative for Atari in this branch.
- Do not assume legacy build systems (`Makefile.win`, older per-platform makefiles) match this target.

## 3) Runtime Data Expectations

- Shareware startup expects at least:
- `HUNTBGIN.WAD`
- `REMOTE1.RTS`
- Typical startup logs include "Adding HUNTBGIN.WAD" and "Adding REMOTE1.RTS".
- Keep data file path handling compatible with Atari-style filesystem behavior.

## 4) Critical Atari/MiNT Touchpoints

These files contain the key platform-specific behavior. Prefer targeted edits here.

- `rott/atari_megaste.c`
- Sets Mega STE 16MHz+cache mode via `Supexec`.
- Exposes `is_megaste()` and `megaste_enable_16mhz_cache()`.
- Defines `_stksize` for MiNT process stack headroom.

- `rott/rt_main.c`
- Calls Mega STE fast mode early in `main()` when `__MINT__` is defined.
- Keeps startup sequence ordering that affects memory and SDL init.

- `rott/modexlib_sdl.c`
- Main SDL video/input backend for this branch.
- MiNT path logs free memory before mode set.
- Uses safer video mode fallback logic.
- Supports non-8bpp display mode fallback through an internal 8-bit blit surface.

- `rott/z_zone.c`
- Zone allocator init and heap sizing.
- MiNT path uses `Malloc(-1L)` in `Z_AvailHeap()`.
- MiNT reserves memory headroom for SDL/GEM allocations after zone setup.
- MiNT bypasses the old "less than 8MB" blocking warning/pause.

- `rott/w_wad.c`
- MiNT avoids large `alloca()` for WAD lump tables.
- Uses heap allocation for lump metadata on MiNT.
- Includes safer lump-name comparison path for Atari alignment constraints.

- `rott/rt_cfg.c`
- MiNT default for `sdl_fullscreen` is off.

- `rott/audio_stubs.c`
- Audio is intentionally stubbed for this target branch. Keep startup stable over feature completeness.

## 5) Platform Macro Reality

- Build currently defines `PLATFORM_UNIX=1` for this target.
- Atari-specific code in this branch is primarily guarded with `#if defined(__MINT__)`.
- Do not rely on `PLATFORM_ATARI` in this branch unless you also update build defines accordingly.

## 6) Memory and Stability Rules (Important)

- This target is memory-constrained (common test setup: 4MB Mega STE).
- Avoid large stack allocations in startup and rendering paths.
- Prefer heap allocations with explicit free paths over `alloca()` for large buffers.
- Avoid unaligned integer casts on raw byte/name arrays in hot lookup code.
- Keep startup memory pressure low before `SDL_SetVideoMode`.
- If changing zone sizing, preserve enough headroom for SDL/GEM driver allocations.

## 7) Video Path Guidance

- Keep initial resolution at `320x200` unless intentionally changing engine assumptions.
- Avoid introducing mandatory fullscreen on MiNT.
- Keep palette path valid for both:
- Native 8bpp video surfaces.
- Non-8bpp display with internal 8-bit blit surface conversion.
- If SDL init fails, preserve clear error messages with `SDL_GetError()`.

## 8) Hatari Validation Baseline

Use a consistent baseline when validating regressions:

- Machine: Mega STE
- CPU: 68000 @ 16MHz
- Cache: enabled (or Mega STE default CE mode)
- RAM: 4MB
- TOS: 2.06
- Video: RGB 50Hz

If startup fails, capture the last printed log line and bomb count/screen state.
For this codebase, that is usually the fastest way to locate the next failing subsystem.

## 9) Known Failure Signatures and Where to Look

- Failure around WAD startup or lump scanning:
- Check `rott/w_wad.c` first.

- `SDL_SetVideoMode failed: Can not alloca` or similar during startup:
- Check stack size (`rott/atari_megaste.c`) and pre-video memory headroom (`rott/z_zone.c`).
- Check SDL mode fallback behavior (`rott/modexlib_sdl.c`).

- Crashes after startup banners but before gameplay:
- Check memory ownership/lifetime around startup allocations.
- Check alignment-sensitive lookup code and platform-specific branches.

## 10) Editing Guidelines for This Repo

- Make small, surgical changes with clear platform guards.
- Keep non-Atari behavior unchanged unless explicitly requested.
- Maintain C89/GNU89-friendly style (toolchain uses `-std=gnu89`).
- Avoid large refactors while startup stability is still being tuned.
- Prefer changing one subsystem at a time and rebuilding after each step.

## 11) Recommended Dev Loop

1. Apply a small patch.
2. Rebuild with `stcmd make`.
3. Validate in Hatari baseline config.
4. Record last visible startup lines.
5. Iterate.

For diagnostics, short temporary `printf` instrumentation in startup-critical paths is acceptable.
Remove or gate noisy logging once issue is resolved.

## 12) Branch Hygiene and Scope

- This branch should ignore legacy/alternative historical implementations unless specifically referenced.
- Use other branches only as targeted reference for a concrete issue, then port minimally.
- Do not mix unrelated platform experiments into Atari startup fixes.

## 13) What "Done" Looks Like for Atari Work

- `stcmd make` succeeds.
- `build/ROTT.TOS` produced.
- Startup reaches interactive menu/gameplay on Hatari Mega STE baseline.
- No blocking startup warnings requiring keypress for low-memory MiNT target.
- No immediate crash in WAD setup, table build, or SDL video mode init.

