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
- The SDL builds need the STDL submodule and its own Xpad:
  `git submodule update --init --recursive lib/stdl` (see "SDL builds").
- `make sidecart` builds the firmware, on the host only (Pico toolchain)
- `make shareware`: `dist/HUNTBGIN.zip`, the ROTT 1.3 shareware release as
  its installer lays it out (`SHAREWARE_DIR`, default `tmp/ROTT_DOS`: a clean
  install, not the played-in `tmp/ROTT`), every file by name. VENDOR.DOC lets
  the shareware be passed on only whole and unmodified; beside its files the
  zip adds `rott/sound.rot` as `SOUND.ROT` (music and effects on for
  `ROTT_ST.TOS` before atarist.7; DOS ROTT 1.3 deletes it, as version 14).
  This is what goes on the release page (uploaded by hand: CI has no shareware).
- Output: everything in `dist/` (`ROTT_ST.TOS`, the SDL builds, config files,
  the data from `tmp/ROTT`, and the firmware's `<uuid>.uf2`/`.json`)
- Run it in EmuMD (from the host, not stcmd): `make emu` (builds with stcmd
  first; `EMU_ARGS` go to `mdfw run`, e.g. `--headless --frames N --screenshot out.png`)
- Object files: `obj/atarist`, `obj/sdl`, `obj/sdl-030`
- All three games' first text is `DrawRottTitle()`'s (`rt_main.c`), the
  port's own on `__MINT__`: `Rise of the Triad <version>`, the edition
  (`Shareware Version`), the port credit and neilrackett.com/atarist,
  in place of ROTT's `Rise of the Triad Startup  Version 1.4`. The version is
  `ROTT_VERSION`: the newest `v*-atarist.*` tag the checkout has (`dev`
  without), or what the release workflow passes, as it builds before it
  tags. Only `rt_main.o` gets it (`.rott_version` stamps), so a new tag
  rebuilds just that.
- The native build's startup is `rott/atari_crt0.S`, linked first: libcmini's
  crt0 plus a clear of the BSS, as a second run from the desktop on a real
  Mega STE (TOS 2.06) kept the first run's statics.
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
  when it changes. Two builds of the same source on the same machine are
  byte-identical, wherever the repository is: the cartridge header's date
  is fixed at the Mega STE's launch, 1 March 1991, the binary info has no
  build date (`PICO_NO_BI_PROGRAM_BUILD_DATE`) and source paths are mapped
  (`-ffile-prefix-map`); so `target_firmware.h` only changes with the
  firmware. CI uses Arm GNU Toolchain 15.2.Rel1 (pinned by version and
  checksum in `.github/actions/pico-toolchain`, which both workflows
  use), as locally, but its firmware still differs
  from a Mac's: CMake 3.28 (Ubuntu's) links the objects in another order
  than CMake 4.x, so functions land elsewhere.

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
- Atari default view size is 7, 320x168 between the status bars (`rott/rt_view.c`);
  the native build reads nothing else from `config.rot` but `ViewZoom`
  (`ReadAtariConfig`, found by key, so it can sit anywhere in the file).
- C2P zoom/no-overlap logic is in `rott/atari_c2p.c`; it runs only with
  `ViewZoom 1` (off by default), as does the Accelerator's matching rule
  (`view_rect` in `atari_md.c`).
- Palette: the game palette (play and menus) is shown with
  `rott/atari_palette.c`, 16 colours and each colour's dither chosen
  offline for what the game draws (`tools/rottpalette`, generated: don't
  edit it), one table for the STE (`_VDO` cookie) and one for the ST.
  Any other palette (matched by hash) gets the old median cut. With the
  Accelerator both halves of the screen must agree, so the ST sends how
  it shows each palette (16 words, a pen pair and ratio a colour) with it
  (`MD_PAL_PENS`, firmware with `MD_CAP_PENS`): the table is ST-only, and
  a new one needs no firmware. Firmware without it gets the plain palette
  and does the median cut, and so does the ST then; firmware given no
  pens (an older ST build) does too. `MDDEBUG.TXT` in autotest runs shows
  both sides' 16 colours.
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
  `FXVolume` (`ReadAtariSoundToggles`) and never writes `sound.rot`; without
  the file (an install copied from DOS may have none) or a key, music and
  effects are on at the shipped file's volumes.
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
  Rumble (`ATARI_XpadRumble(low, high, vbls)`, provider with
  `XPAD_CAP_RUMBLE`, e.g. COMpad): firing, damage, explosions near the
  player, moving walls, dying. A VBL routine (atari_vbl.c) counts the
  motors down and writes the request area only on a change (COMpad
  sends each over a 200 bytes/s link), so a stalled loop can't leave
  one running, and the terminate vector stops them on any exit.

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
- Level loading (the red and yellow bar): its eight steps are stages of
  `SetupGameLevel`, not loading progress, and the time was CPU, not disk.
  Lump names are looked up through a hash index (`w_wad.c`, built on the
  first lookup); the precache list's duplicate check is a bit a lump
  (`rt_ted.c`); `DrawTiledRegion` copies rows; a palette change works out
  each colour's dither once (`c2p_color_tables`). E1L1, EmuMD: Mega STE
  61 -> 8.4 s, 8MHz over 100 -> 15 s. What's left is the setup functions'
  own passes over the map.
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
  catches up every 64th frame.
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
- Level start with the Accelerator (`ATARI_MD_FinishUpdate`'s snapshot):
  the game clock is paused (`I_PauseTime`/`I_ResumeTime`, i_timer.c) from
  the snapshot to the end of that frame, as catching up on the wait ran
  ten tics a frame on stale input for seconds; then `warm_up` renders the
  start position facing four ways (unseen) so the MD loads from SD now
  what the level opens with. "Preparing level" shows the pack as 0-80%
  and the warm-up as 80-100%.
- `ATARI_MD_BLIT=1` (default): the blitter copies MD frames while the CPU
  runs on; anything else that draws to the screen calls `ATARI_MD_BlitWait()`.
- Most of `ATARI_MD_FinishUpdate` runs in supervisor mode on a Mega STE: no
  `Super(0L)` calls in that stretch, and supervisor exits go through
  `sidecart_md_super_end` (GEMDOS `Super(ssp)` only works at the same stack depth).
- Wire protocol: `sidecart/include/rott_md_protocol.h` (both sides; never hard-code offsets).
- WAD copy: when HELLO finds the card's WAD missing or different, the ST
  sends its own (`MD_CMD_WAD_WRITE`, firmware with `MD_CAP_WAD_COPY`;
  Esc cancels, read through TOS, which still has the keyboard at
  startup), written to `/rott/WADCOPY.TMP` and renamed, then HELLOs
  again. Not in autotest or logic-check builds.
- Startup text: TOS's console needs `\r\n`. libcmini's `printf` sends
  one for each `\n`, but GCC turns a constant `printf("...\n")` into
  `puts()`, which doesn't, so the native build has `-fno-builtin-printf`
  (and `-fprintf`). Print through `printf`, not `puts`, `putchar` or
  `Cconws`. TOS doesn't wrap: keep startup lines within 40 columns.
- Firmware: `make -C sidecart build` (needs `PICO_TOOLCHAIN_PATH`); tests:
  `make -C sidecart tests` (PINGTEST/UPTEST), `make -C sidecart emu`
  (host self-tests: the command protocol and the level pack's on-demand
  loading; no music).
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

- ROTT's own renderer through SDL 1.2's API and MiNTLib, as close to the
  original game as the hardware allows. The SDL is STDL's SDL1 add-on,
  not SDL 1.2 itself (which they used up to atarist.9): `ROTT_SDL.TOS` is
  68000 code for every machine (256 colours on a TT or a Falcon, 16 on an
  ST), `ROTT_030.TOS` 68030 + 68882 code for a TT or a Falcon (its
  `SDL_Init` refuses an ST). SDL-only files: `rott/modexlib_sdl.c`,
  `atari_sdl.c`, `audio_stubs.c`; sources are listed in `SDL_SOURCES`,
  settings are `SDL_*` in the Makefile.
- Both kinds of build are MiNT, so `__MINT__` means "any Atari build". In
  shared code, the native builds' hardware and speed code goes under
  `ATARI_NATIVE` (defined by the native build only) and SDL-only code under
  `ATARI_SDL`. Anything else is in both.
- The native `ROTT_ST.TOS` must not change when SDL work is done: build it
  clean before and after and `cmp` (only `STUB_FUNCTION` line numbers may move).
- SDL builds keep settings in `sdlconf.rot`/`sdlsound.rot` (`rott/sdlconf.rot`,
  `rott/sdlsound.rot`), so they can share a folder with `ROTT_ST.TOS`.
- STDL is the submodule `lib/stdl`, pinned to a release (v1.31.0); it
  carries its own `lib/xpad`, so `git submodule update --init --recursive
  lib/stdl`. The Makefile builds its archives with STDL's own Makefile
  (`libstdl-sdl1.a`, `libstdl-sdl2-st.a` for the 68000 build,
  `libstdl-sdl2.a` for the 68030 one, `libstdl.a`; stamp `obj/.stdl_libs`),
  from the repository's root: built from inside the submodule, the
  container can't see the repository STDL's start-up line takes its
  version from. Its docs: `lib/stdl/docs/sdl1.md`, and `sdl2.md` for the
  screen underneath.
- What STDL asks of the game:
  - `atari_sdl.c`: one `SDL_SetVideoMode(320, 200, 8)`, always full
    screen: no driver to pick and no GEM window (`FullScreen 0` and
    `WINDOW` do nothing). `SDL_QuitSubSystem` stops nothing, so closing
    video is `SDL_CloseAudio` then `SDL_Quit`, which is what puts the
    desktop's screen back for the text `Error()` and `QuitGame()` print.
  - STDL keeps the program in supervisor mode from `SDL_Init` to
    `SDL_Quit`: `Super(0L)` there is the way out of it, so `i_timer.c`
    asks `Super(1L)` first. Any new `Super()` pair needs the same.
  - `modexlib_sdl.c` draws straight into the screen surface (no buffer of
    its own, no copy a frame), and polls events before and after
    `SDL_Flip`: STDL refills the sound's ring from the event pump alone
    (no interrupt), at least every 150ms, so a frame longer than that
    breaks the sound up.
  - `audio_stubs.c` (effects and the MIDI songs as square waves, one
    mixer) asks for signed 8 bits at 12517Hz, the DMA sound's own format,
    which STDL plays with no conversion pass; one channel or two by
    `NumChannels`. STDL calls it for 2048 frames at a time, so it mixes a
    voice at a time over a block, volumes worked out once a block: keep
    divisions and per-voice tests out of the loops a sample. With sound on,
    Hatari's TT (2MB + 4MB, `SDL_BENCH`): 6.31 fps against 6.80 with it
    off; the mixer before (16 bits, stereo, 11025Hz, a sample at a time)
    4.17, and SDL 1.2 0.12 with noise on one channel (its 8-bit device
    was written 16-bit samples). On by default in `sdlsound.rot`; the
    music is quiet beside the effects (`MusicVolume`).
  - `SDL_WasInit` answers with the flags the game asked for and nothing
    more: `SDL_Init` of anything starts all of STDL, and
    `SDL_InitSubSystem`/`SDL_QuitSubSystem` start and stop nothing. Don't
    add stubs or `#define`s of the add-on's names: a macro of ours hides
    the add-on's without a word.
- `SDL_BENCH=N`: straight into E1A1, turning on the spot by the clock for
  N seconds, then the frames drawn are added to `BENCH.TXT` and the game
  quits (`ATARI_SDL_BenchFrame`, `atari_sdl.c`). With `SDL_TARGET_FPS=0`,
  or the 12 a second cap is what gets measured. Give each build its own
  `BUILDDIR` and object folders.
- Why STDL (2026-11-16, Hatari's TT, TOS 3.06, 4MB + 16MB of TT-RAM,
  `SDL_BENCH=30 SDL_TARGET_FPS=0`, frames a second on STDL against on SDL
  1.2): the 68030 build 6.77 against 5.84, the 68000 build 4.99 against
  4.48. With no TT-RAM 5.52 against 4.59 for the 68030 build, and the
  68000 one ran out of zone on SDL 1.2 (`Z_Malloc`) where it runs on STDL
  (4.01). 2MB + 4MB of TT-RAM: 6.76 and 5.03 on STDL. A Falcon on VGA:
  2.18 against 1.91, and 1.52 against 1.38. No real machine had run them
  when the builds were switched, and STDL's own notes have a real TT at
  2.3 times Hatari's times, three to four over ST-RAM copies. The SDL
  builds are for a TT first: on an ST the add-on's 16-colour screen is
  for testing (a fade step is 135ms there, a new palette about a second).
  Nobody had listened to the sound on STDL either.
