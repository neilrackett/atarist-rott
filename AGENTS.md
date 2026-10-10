Project: Rise of the Triad Atari ST/Mega STE Port (atari-st-c2p)

Purpose

- Keep this branch focused on the native Atari renderers: the direct C2P
  renderer, and the ROTT Accelerator (SidecarTridge Multi-device) path.
  The SDL builds (from the old `sdl` branch) share the source but aim to
  stay close to the original game: see "SDL builds" below.
- Prioritize playability and responsiveness on 16 MHz Mega STE.
- Keep non-Atari behavior unchanged unless intentionally doing cross-platform work.

Build + Artifacts

- `make` (= `make games`) builds the three games; `make st` (`ROTT_ST.TOS`),
  `make sdl` (`ROTT_SDL.TOS`), `make sdl-030` (`ROTT_030.TOS`). From the host
  they go through stcmd (`JOBS=4` by default); inside stcmd they build directly.
- `make sidecart` builds the firmware, on the host only (Pico toolchain)
- `make shareware`: `dist/HUNTBGIN.zip`, the ROTT 1.3 shareware release as
  its installer lays it out (`SHAREWARE_DIR`, default `tmp/ROTT_DOS`: a clean
  install, not the played-in `tmp/ROTT`), every file by name. VENDOR.DOC lets
  the shareware be passed on only whole and unmodified; this is what goes on
  the release page (uploaded by hand: CI has no shareware).
- Output: everything in `dist/` (`ROTT_ST.TOS`, the SDL builds, config files,
  the data from `tmp/ROTT`, and the firmware's `<uuid>.uf2`/`.json`)
- Run it in EmuMD (from the host, not stcmd): `make emu` (builds with stcmd
  first; `EMU_ARGS` go to `mdfw run`, e.g. `--headless --frames N --screenshot out.png`)
- Object files: `obj/atarist`, `obj/sdl`, `obj/sdl-030`
- CI (`.github/workflows`): pull requests to `main` build the three games
  and the firmware; anything landing on `main` is also tagged with the next
  `v1.4.0-atarist.N` (REminiscence style: ROTT 1.4, then a count; a tag put
  on the commit by hand wins), replaces the `.TOS` files on the `latest`
  release and then moves the `latest` tag. The firmware (`<uuid>.uf2` and
  `<uuid>.json`, needs the `APP_UUID_KEY` secret) is only rebuilt and
  replaced when something it's built from has changed since the commit
  `latest` marks: `sidecart/` apart from its documentation,
  `rott/atari_tables.*` or `rott/audiolib/atari_music.*`. A manual run of the workflow can force it, which
  is also the fix if `latest` gets moved by hand. Documentation-only
  changes (`.md` files, `doc/`, `COPYING`, `LICENSE`) run neither workflow.
  The firmware ships `sidecart/version.txt` as it is, so bump it by hand
  when it changes.

Current Makefile Defaults

- `ATARI_ENABLE_FASTMODE=1`
- `ATARI_ENABLE_KBDINT=1`
- `ATARI_SHOW_FPS=1`
- `ATARI_TARGET_FPS=5`
- `ATARI_C2P_VIEW_ZOOM=1`
- `ATARI_C2P_STRICT_NO_OVERLAP=1`
- `ATARI_NOIR=0`
- `ATARI_MAX_CATCHUP_STEPS=0`
- `ATARI_CATCHUP_POLL_INTERVAL=4`
- `ATARI_MENU_CURSOR_DELAY_TICS=0`
- `ATARI_MENU_EVENT_DRIVEN=1`
- `ATARI_SKIP_PRECACHE=1`
- `ATARI_SKIP_FADES=1`
- `ATARI_SKIP_LIGHTLEVEL=1`
- `ATARI_SKIP_FIZZLE=1`

Important Current Behavior

- Low-memory mode is forced (`rott/z_zone.c`: `lowmemory=1`).
- Atari default view size is tuned small by default (`rott/rt_view.c`, `rott/rt_cfg.c`).
- C2P zoom/no-overlap logic is in `rott/atari_c2p.c`.
- Atari movie playback is intentionally static-last-frame style in `rott/cin_main.c`.
  - Static behavior is now code-based constants, not `ATARI_CINEMATIC_*` flags.
- First title flow after logo jumps to menu on Atari (`rott/rt_main.c`).

Input + Timing Notes (Critical)

- Input pumping is required in active loops (`IN_PumpEvents()`).
- Avoid reintroducing aggressive catch-up limits under load.
  - Keep `ATARI_MAX_CATCHUP_STEPS=0` unless specifically profiling alternatives.
- If gameplay appears frozen but rendering updates, suspect timing/input path first.
- Sound effects: `rott/atari_sfx.c`, the `FX_` calls on DMA sound (STE, Mega
  STE), in place of the DOS library's `fx_man.c`/`multivoc.c`. A VBL routine
  mixes a 1KB ring at 12517Hz (the mixer is ported from STDL's voice mixer);
  `NumVoices` in `sound.rot` is 1-8, or 0 for 2 with the Accelerator, 1
  without. On by default (`FXMode 6`); an ST without DMA sound skips them.
  The native build reads only `MusicMode`, `FXMode`, `NumVoices` and
  `FXVolume` (`ReadAtariSoundToggles`) and never writes `sound.rot`.
- Music: ROTT's MIDI songs on the YM2149 (`rott/audiolib/atari_music.c`, the
  `MUSIC_` calls in `atari_music_api.c`), stepped at 50Hz from the VBL off
  the 200Hz clock; YM registers only in supervisor mode, select and write
  with interrupts masked. TOS's key click and bell are off while it plays.
  On by default (`MusicMode 6`); `MusicVolume` 0-255. With the Accelerator
  (`MD_CAP_MUSIC`, `ATARI_MD_MUSIC=1`) the firmware runs the same player
  (`sidecart/rp/src/md_music.c`, `atari_music.c` built with `YMMUSIC_MD`)
  and the VBL copies its registers (`ATARI_MD_MusicVbl`, the `MD_YM_*`
  ring); check changes at 8MHz in EmuMD (its clock is wrong after the
  Mega STE's switch to 16MHz).
- `rott/atari_vbl.c`: one TOS VBL queue slot for the sound effects mixer and
  the music, and their hardware put back through the terminate vector on any
  exit (`Error()`, a crash). Anything new on the VBL goes through it.
- Gamepads: Xpad (submodule `lib/xpad`, consumer half only), `rott/atari_xpad.c`.
  In play it sets `buttonpoll[]` and `JX`/`JY` from `PollControls`; outside play
  (menus, key waits) it injects keys from `doEvents`. Only providers that read
  hardware work: the IKBD example drivers hook TOS, which our ACIA handler bypasses.

Performance Notes

- Measure at 8MHz (EmuMD with `-- --machine ste --cpuclock 8`): what is
  faster at 16MHz is a bonus.
- Game logic runs once per tic (35 a second) and a slow frame is followed
  by more tics of it, up to 10: logic costs the frame rate far more than
  its share of the CPU, and on E1L1 at 8MHz it fills the tics on its own.
  With the Accelerator the ST, not the Multi-device, sets the frame rate.
  Per-object costs that run every tic (actor list walks, elevator disks,
  moving walls) are the ones that matter; `ATARI_LOGIC_CHECK` proves a
  change exact. Already done that way (rt_actor.c): the active-actor loop
  is `ATARI_DoActiveActors`, with elevator disks through `DoElevDiskSteps`
  (keep it in step with `DoActor`); area walks skip by hand
  (`NextInRangeXY`). `I_GetTime()`/`GetTicCount()` are macros on the
  native build (i_timer.h, isr.h). Check new code for libgcc arithmetic
  (`__mulsi3`, `__divsi3`, 64-bit, soft float) with STDL's
  `tools/libcalls.sh` on the objects: about 350-3000 cycles a call.
- `ATARI_LOD=1` (default): game logic level of detail, not exact (logic-
  check builds default to 0, which stays exact). Moving walls more than 5
  tiles from the player and elevator disks more than 3 move 4 tics at a
  time (`WallMovingSteps`, `ElevDiskThinkSteps`); ordinary enemies that
  are idle, more than 8 tiles away and out of sight join the actor
  throttle (`ATARI_IdleOutOfSight`); disks go last in their area's actor
  list, so walks that can't meet one stop there (`firstareadisk`,
  `ATARI_DiskNear`). E1L1 at 8MHz: 4.8 -> 14 fps over a profile window,
  3.2 -> 6.1 fps at the overlay test spot. Distances in rt_actor.h.
- Mega STE speed (`rott/atari_megaste.c`): set only bits 0-1 of
  `$FFFF8E21`, read-modify-write, as STDL does; a bare `0x03` clears bits
  the control panel sets, and Hatari can't show the difference.
- Menu responsiveness depends heavily on event-driven menu path.
- Disabling expensive effects has a larger impact than minor micro-optimizations.
- `ATARI_NOIR=1` is available for grayscale palette testing and can help performance experiments.

Testing Workflow (Hatari Mega STE)

1. Build with `make st`.
2. Boot to menu and verify immediate key response.
3. Start a level and verify timer advances and controls work immediately.
4. Verify viewport scaling and HUD no-overlap behavior.
5. Verify FPS overlay appears when `ATARI_SHOW_FPS=1`.

Debugging Guidance

- Keep `ATARI_DEBUG=0` for normal runs.
- Use `ATARI_DEBUG=1` only temporarily and remove added logging once done.
- Prefer small isolated changes and test after each.

Common Pitfalls

- Re-enabling heavy cinematic/effect paths causes major regressions quickly.
- Startup prints from old debugging can affect perceived stability/usability.
- Confusing `ATARI_SHOW_FPS` with old `ATARI_ENABLE_FPS` naming.

ROTT Accelerator (MD/ROTT in the source; SidecarTridge Multi-device renderer)

- `ATARI_MD_RENDER=1` (default): `ROTT_ST.TOS` looks for the MD/ROTT
  firmware at startup and, if found, the Multi-device renders the 3D view;
  otherwise the C2P renderer runs as before. See `sidecart/README.md`.
- ST side: `rott/atari_md.c` (+ `sidecart_md.c`, `sidecart_stubs.S`,
  `atari_md_s.S`); hooks are guarded by `ATARI_MD_RENDER` / `ATARI_MD_Active()`.
- Any tilemap write needs `MD_TILE_TOUCH(x, y)` so the MD's world mirror follows;
  any masked wall write (flags or textures) needs `MD_MASKED_TOUCH()`.
- Any 2D drawing into the chunky screen outside the view should say so
  (`ATARI_HUD_TOUCH()`, or `_RECT`/`_AT` with where), or the HUD only
  catches up every 16th frame.
- The Help key toggles an overlay: with the Accelerator, frames a second,
  game logic tics run each frame and their time (`TICS`, `LOGIC`), the
  ST's wait for it each frame (`wait_ready`), its render and dither + c2p
  time (from the status block), its SD loads during play, then sound
  effects playing and late mixer refills, and `CPU`: on a Mega STE
  `$FFFF8E21` as TOS left it and as set, then a speed probe (passes/100 of
  a loop in 50ms: about 74 at 8MHz, 150 at 16MHz with the cache, in
  Hatari). Without the Accelerator only the last three. The way to see on real hardware
  where frames go: EmuMD runs the firmware natively, so its render times
  (and SD loads) say nothing about a real RP2040. `TICS 10.0` is the
  catch-up cap (`MAXTICS`): the frame rate is then set by game logic.
- `ATARI_MD_BLIT=1` (default): the blitter copies MD frames while the CPU
  runs on; anything else that draws to the screen calls `ATARI_MD_BlitWait()`.
- Most of `ATARI_MD_FinishUpdate` runs in supervisor mode on a Mega STE: no
  `Super(0L)` calls in that stretch, and supervisor exits go through
  `sidecart_md_super_end` (GEMDOS `Super(ssp)` only works at the same stack depth).
- Wire protocol: `sidecart/include/rott_md_protocol.h` (both sides; never hard-code offsets).
- Firmware: `make -C sidecart build` (needs `PICO_TOOLCHAIN_PATH`); tests:
  `make -C sidecart tests` (PINGTEST/UPTEST), `make -C sidecart emu`,
  `make -C sidecart hatari`, `sidecart/tests/emu/run-hatari.sh`.
- EmuMD (submodule `sidecart/emu/emumd`): in `sidecart/`,
  `emu/emumd/tools/mdfw build` / `run --headless --frames N --screenshot
  out.png --log out.log` (config in `sidecart/mdfw.ini`, glue in `sidecart/emu/`).
- Test builds: `ATARI_MD_AUTOTEST=8` starts a game, turns a fixed step per
  frame and saves `SHOTnnn.PI1` + `MDDEBUG.TXT`, so MD and ST runs compare;
  add `ATARI_MD_AUTOTEST_DIE=N` to be killed at frame N (death sequence).
- Fades: `ATARI_SKIP_FADES` makes `VL_Fade*` instant. `atari_c2p_fade()` does
  real fades with the 16 colour registers alone (cheap); the death sequence
  fades out with it and stays dark until the next screen's `VL_FadeIn` or
  `RefreshMenuBuf` fades back in.
- `ATARI_LOGIC_CHECK=4` builds play the same scripted game on a virtual clock
  (4 tics a frame) and write a game-state hash per frame to `LOGIC.TXT`:
  identical files before and after a change mean the game logic is unchanged.
  Run one before touching game logic for speed (see `sidecart/README.md`).

Change Discipline

- Put Atari-specific logic under `#if PLATFORM_ATARI` where practical.
- Avoid broad refactors while chasing perf/input regressions.
- Keep behavior changes documented in commit messages with observed impact.

SDL builds (`ROTT_SDL.TOS`, `ROTT_030.TOS`)

- ROTT's own renderer through SDL 1.2 and MiNTLib (both in the stcmd image),
  as close to the original game as the hardware allows. SDL-only files:
  `rott/modexlib_sdl.c`, `atari_sdl.c`, `audio_stubs.c`; sources are listed in
  `SDL_SOURCES`, settings are `SDL_*` in the Makefile.
- Both kinds of build are MiNT, so `__MINT__` means "any Atari build". In
  shared code, the native builds' hardware and speed code goes under
  `ATARI_NATIVE` (defined by the native build only) and SDL-only code under
  `ATARI_SDL`. Anything else is in both.
- The native `ROTT_ST.TOS` must not change when SDL work is done: build it
  clean before and after and `cmp` (only `STUB_FUNCTION` line numbers may move).
- SDL builds keep settings in `sdlconf.rot`/`sdlsound.rot` (`rott/sdlconf.rot`,
  `rott/sdlsound.rot`), so they can share a folder with `ROTT_ST.TOS`.
