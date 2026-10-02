Project: Rise of the Triad Atari ST/Mega STE Port (atari-st-c2p)

Purpose

- Keep this branch focused on the native Atari renderers: the direct C2P
  renderer, and the ROTT Accelerator (SidecarTridge Multi-device) path.
- Prioritize playability and responsiveness on 16 MHz Mega STE.
- Keep non-Atari behavior unchanged unless intentionally doing cross-platform work.

Build + Artifacts

- Primary build command: `stcmd make`
- Parallel build: `stcmd make -j4`
- Output executable: `build/atarist/ROTT_ST.TOS`
- Run it in EmuMD (from the host, not stcmd): `make emu` (builds with stcmd
  first; `EMU_ARGS` go to `mdfw run`, e.g. `--headless --frames N --screenshot out.png`)
- Object files: `obj/...`
- CI (`.github/workflows`): pull requests to `atarist` build the game and the
  firmware; anything landing on `atarist` also moves the `latest` tag and
  replaces `ROTT_ST.TOS`, `<uuid>.uf2` and `<uuid>.json` on the `latest`
  release (needs the `APP_UUID_KEY` secret). The firmware ships
  `sidecart/version.txt` as it is, so bump it by hand before a release.

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
- Gamepads: Xpad (submodule `lib/xpad`, consumer half only), `rott/atari_xpad.c`.
  In play it sets `buttonpoll[]` and `JX`/`JY` from `PollControls`; outside play
  (menus, key waits) it injects keys from `doEvents`. Only providers that read
  hardware work: the IKBD example drivers hook TOS, which our ACIA handler bypasses.

Performance Notes

- Menu responsiveness depends heavily on event-driven menu path.
- Disabling expensive effects has a larger impact than minor micro-optimizations.
- `ATARI_NOIR=1` is available for grayscale palette testing and can help performance experiments.

Testing Workflow (Hatari Mega STE)

1. Build with `stcmd make`.
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
