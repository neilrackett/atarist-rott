Project: Rise of the Triad Atari ST/Mega STE Port (atari-st-c2p)

Purpose

- Keep this branch focused on the direct Atari C2P renderer.
- Prioritize playability and responsiveness on 16 MHz Mega STE.
- Keep non-Atari behavior unchanged unless intentionally doing cross-platform work.

Build + Artifacts

- Primary build command: `stcmd make`
- Parallel build: `stcmd make -j4`
- Output executable: `build/ROTT.TOS`
- Object files: `obj/...`

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

Change Discipline

- Put Atari-specific logic under `#if PLATFORM_ATARI` where practical.
- Avoid broad refactors while chasing perf/input regressions.
- Keep behavior changes documented in commit messages with observed impact.
