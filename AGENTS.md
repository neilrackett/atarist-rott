Project: Rise of the Triad Atari ST/Mega STE Port (atari-st-c2p)

Purpose

- Keep this branch focused on the native Atari renderers: the direct C2P
  renderer, and the ROTT Accelerator (SidecarTridge Multi-device) path.
  The SDL build (from the old `sdl` branch) shares the source but aims to
  stay close to the original game: see "SDL build" below.
- Prioritize playability and responsiveness on 16 MHz Mega STE.
- Keep non-Atari behavior unchanged unless intentionally doing cross-platform work.

Build + Artifacts

- `make` (= `make games`) builds the two games; `make st` (`ROTT.TOS`, the
  native build: one 68000 program for the whole ST family, called
  `ROTT_ST.TOS` up to atarist.9) and `make sdl` (`ROTT_SDL.TOS`, 68030
  code for a TT or a Falcon; until the rename a 68000 build, with a
  68030 + 68882 `ROTT_030.TOS` beside it). From the host they go through
  stcmd (`JOBS=4` by default); inside stcmd they build directly. The
  release workflow takes any `.TOS` file no longer built off the release.
- Both games need the STDL submodule and its own Xpad:
  `git submodule update --init --recursive lib/stdl` (see "SDL build").
- `make sidecart` builds the firmware, on the host only (Pico toolchain)
- `make shareware`: `dist/HUNTBGIN.zip`, the ROTT 1.3 shareware release as
  its installer lays it out (`SHAREWARE_DIR`, default `tmp/ROTT_DOS`: a clean
  install, not the played-in `tmp/ROTT`), every file by name. VENDOR.DOC lets
  the shareware be passed on only whole and unmodified; beside its files the
  zip adds `rott/sound.rot` as `SOUND.ROT` (music and effects on for
  the native build before atarist.7; DOS ROTT 1.3 deletes it, as version 14).
  This is what goes on the release page (uploaded by hand: CI has no shareware).
- Output: everything in `dist/` (`ROTT.TOS`, `ROTT_SDL.TOS`, config files,
  the data from `tmp/ROTT`, and the firmware's `<uuid>.uf2`/`.json`)
- Run it in EmuMD (from the host, not stcmd): `make emu` (builds with stcmd
  first; `EMU_ARGS` go to `mdfw run`, e.g. `--headless --frames N --screenshot out.png`)
- Object files: `obj/atarist`, `obj/sdl`
- Both games' first text is `DrawRottTitle()`'s (`rt_main.c`), the
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
- CI (`.github/workflows`): pull requests to `main` build the two games
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
- `ATARI_EIGHT=1`
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
  and `Colours` (`ReadAtariConfig`, found by key, so they can sit anywhere
  in the file).
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

TT and Falcon: 256 colours (`ATARI_EIGHT=1`, `rott/atari_eight.c`)

- `ROTT.TOS` on a TT or a Falcon with a colour monitor shows the game's
  own 256 colours: TT Low (each row on two lines) or a Falcon's 8-bit mode
  (320x240 on VGA, 320x200 on RGB or a TV at the desktop's PAL or NTSC),
  eight interleaved planes, the 320x200 picture in the middle. No
  reduction, no dither, no palette table. `Colours 16` in `config.rot`
  keeps ST Low, as does a mono monitor or no ST RAM for the page.
- Still 68000 code, one program for every machine. The mode set, the
  monitor tests and the way back to the desktop follow STDL
  (`src/responsive.c`, `src/video.c`); none of STDL runs beside the game
  (it would want the keyboard, the VBL and supervisor mode for itself).
  The conversion is STDL's, compiled from the submodule as it stands:
  `lib/stdl/addons/sdl2/src/c2p.c`. So `make st` needs `lib/stdl` now
  (`ATARI_EIGHT=0` builds without).
- One page, in ST RAM (`Mxalloc` mode 0, checked against `phystop`),
  allocated before `Z_Init` takes the memory (`atari_eight_prepare`) and
  given back if `Colours 16` or an Accelerator that cannot send 8 planes
  says so (`atari_eight_cancel`); the mode is set in `GraphicsMode`
  (`atari_eight_enter`). The colours go to the registers in the vertical
  blank (atari_vbl.c), and that routine's release puts TOS's mode, screen
  and all its colours back, on the terminate vector too.
- `atari_c2p.c` asks `atari_eight` at its entries: the whole screen is the
  rows' changed groups of 16 pixels, a box at a time (`eight_screen`), the
  HUD beside the Accelerator's view its changed tiles as before, a fade
  the 256 registers. `ViewZoom` zooms as on an ST (`eight_zoom`: the ST's
  geometry and clipping, each zoomed row built at 320 pixels and its span
  converted with STDL's pixel-pair loop, built into the game as
  `rott/atari_c2pp.c`; a full-width row's repeats copied). Checked pixel
  for pixel against the chunky screen for view sizes 0-2
  (`tools/bench/zoomcheck.py`; autotest shots `SHOTnnn.P8` beside
  `CHUNKnnn.RAW`). View size 2 at 2x: TT 2+4MB 65ms a frame, against 100
  at full size.
- With the Accelerator (`MD_HELLO_EIGHT`, firmware with `MD_CAP_EIGHT`,
  v1.4.0-beta.8): the view comes as eight planes, the pixels as rendered
  (`md_video_c2p8`), and `atari_md_copy8` copies it, a TT's second line
  from the registers. A frame is up to 53,760 bytes, exactly both frame
  buffers, so there is one buffer: the ST waits for the frame it sent and
  copies it before it sends the next (`ATARI_MD_FinishUpdate`), and the MD
  draws while the ST runs the game, never during a copy. Older firmware
  has no `MD_CAP_EIGHT` and the game stays in ST Low for it.
- A 68030's data cache can answer a cartridge read as a Mega STE's cache
  can: `sidecart_md_bus_begin`/`_end` turn it off through CACR for the
  same stretches (`md_cache_off`, the music's VBL too), and
  `ATARI_MD_FinishUpdate` runs in supervisor mode there as on a Mega STE.
  From the 68010 on the supervisor test is `Super(1L)`, not MOVE from SR.
  A 68040's or 68060's is written out (CPUSHA) and turned off the same
  way. Hatari's data cache does not go stale on the cartridge, so only a
  real machine can show this working, as it showed md-doom's.
- Checked in EmuMD's Hatari only (2026-11-20; TT with TOS 3.06, Falcon
  with TOS 4.04 on VGA and RGB): title, menus and E1L1 with and without
  the Accelerator, the quit back to the desktop in its own mode and
  colours, `Colours 16`, the firmware before (ST Low, as it was), a
  68040 Falcon; a Mega STE's `SHOTnnn.PI1` byte for byte as before with
  both renderers. No real TT or Falcon has run it.
- Frames a second there, autotest build: with the Accelerator 35 on both
  (the tic rate: EmuMD's firmware runs at the host's speed, and a real
  TT's copy from the cartridge is what will set it, as md-doom's 12-15);
  the ST renderer 4 on a TT and 3 on a Falcon in 256 colours, 3 and 3 in
  16. Without an Accelerator the 68030 build then was the faster game there
  (`ROTT_SDL.TOS` is that build now, without the 68882).
- Where a TT's frame goes with the ST renderer (Hatari's profiler,
  cycle-exact, 20s of the autotest in E1L1, 256 colours, full-width
  view): 227ms a frame, 4.4 a second. Drawing the columns a pixel at a
  time is 109ms of it (`R_DrawWallColumn` 20%, `ScaleMaskedPost` 9%,
  `DrawWallPost`, `InterpolateMaskedWall`, `DrawSkyPost`); the screen
  38ms (STDL's conversion 21, the compare and the copy kept of the last
  frame 16); libgcc's 32-bit multiplies and divides 31ms, as the game is
  68000 code; game logic about 5%. The same sources compiled `-m68030`
  (an experiment, not a build the Makefile has): 190ms, 5.25 a second,
  the library calls down to 6ms and nothing else moved. So the 68000
  build costs a TT a fifth, and the rest is ROTT drawing 54,000 pixels
  and more a frame in C. A real TT takes 2.3 times Hatari's time or more.
- Autotest builds save `SHOTnnn.P8` in 256 colours: 768 bytes of colours,
  then the screen read back out of its planes, 320x200, a byte a pixel.

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
- The ST renderer's speed work (2026-11-20; Hatari, autotest turning a
  full circle at the start of E1L1, low detail, full-width view, ms a
  frame): ST at 8MHz 2,708 -> 1,358; TT 233 -> 127; TT with 2MB of ST RAM
  and 4MB of TT RAM 189 -> 99 (with the low-detail approximations below
  and the steps after them: ST 986, TT 102, TT 2+4MB 77.7). Every step
  but those approximations drew the same pixels and ran the
  same game logic (tools/bench, below). What changed:
  - Column loops in assembler (`rott/atari_col.S`): walls, sprites,
    masked and translucent posts (`atari_post_masked`, `atari_post_trans`:
    the whole of ScaleMaskedPost/ScaleTransparentPost), sky, floor and
    ceiling rows (`atari_row`). A texture position is a fraction word and
    a row word stepped with add.w/addx.w, eight rows a pass entered part
    way (Duff), no library multiply anywhere in them.
  - Walls: rt_draw.c's `DrawWallsFast` sets each post up in locals and
    notes its columns (`atari_wallcol_t`, rt_dr_a.h); `atari_col_walls`
    draws a frame's notes in one call. With `ATARI_FLAT_WALL_LIGHT` every
    wall goes through one shading table, so each wall texture is shaded
    once into an 8K zone copy, each texel in both bytes of a word
    (`ShadedWall`, PU_CACHE, `shadedwall[lump]`): no table a pixel, and
    low detail's doubled columns are one move.w a row. The position is
    doubled to read it (the row is still floor(position) & 63). Anything
    that may allocate (a new copy, a lump load) draws the notes first, as
    the zone may purge what they point at.
  - The 3D view is converted without comparing it to the last frame
    (`atari_c2p_view_drawn`, rt_draw.c after WallRefresh): it is drawn
    afresh every frame; the HUD round it is still compared.
  - The 68020's own multiply and divide on a TT or a Falcon, chosen at run
    time (`rott/atari_cpu.S`, `atari_cpu_init`): libgcc's __mulsi3,
    __udivsi3, __divsi3, __umodsi3, __modsi3, __muldi3 and __divdi3 are
    wrapped at the link (`ATARI_WRAPS`), as are FixedMul and FixedScale,
    each to the bit what the 68000 path gives (game logic must not depend
    on the machine). FixedScale on a 68000 is four mulu.w and a 64/32
    division of its own (the ray cast's every wall hit, 6,600 cycles each
    through libgcc). A faster 68000 __udivsi3 was tried and measured the
    same as libgcc's: divu.w itself is the cost.
  - The zone is sized from the larger of ST RAM and TT RAM (`Z_AvailHeap`):
    asked of ST RAM alone, a TT with 2MB of it and 4MB of TT RAM got 1.4MB
    and stopped at ROTT's low-memory warning.
  - `Interpolate` (engine.c): its divides by 2 or 4 are shifts, rounded
    toward zero as C's.
  - ViewZoom on an ST: the zoomed rows are converted straight from the
    view's rows (they were copied out a byte at a time), and the rows
    beside the zoom only when they change (`zoom_other_row`, `zoom_ok`).
    View size 2 at 2x, 8MHz: 928 -> 739ms. An `ATARI_VIEWSIZE=n` build
    starts a game at view size n, for testing it.
  - Low detail (`DetailLevel 0`, the default; walls already drawn a
    column for each pair) also draws, at half the width:
    - the sky, floors and ceilings (`ATARI_LOWDETAIL_FLATS=1`), as DOS
      ROTT's low detail does them (`DrawSky` and `DrawHLine`'s DOS
      branches): a pixel fetched for each pair, from its left column,
      stored to both (`atari_col_sky2`, `atari_row2`);
    - masked walls, the railings and gates (`ATARI_LOWDETAIL_MASKED=1`),
      as its sprites are (ScaleShape's doublestep branch): an even column
      drawn into two (`atari_post_masked2`, `atari_post_trans2`, a
      translucent span still shading each column's own pixels).
    These change pixels, so they were judged by eye (indistinguishable at
    normal size) and the game logic checked identical; `=0` restores full
    width. With them: ST 1,341 -> 1,190ms a frame, TT 128 -> 107, TT
    2+4MB 100 -> 84. High detail is unchanged.
  - The wall set-up's usual post (no alternate tile, a height under
    65536) is atari_col.S's `atari_walls_run`, the C's arithmetic in the
    same order, its height caches shared with `DrawWallsFast` (the
    `atari_wallrun_t` it reads by offset: keep the two in step); any other
    post goes back to the C. On a 68020 or later low detail's word loop
    reads the word copy with a scaled index (`(a0,d1.w*2)`, the W3 loop,
    chosen at run time): four instructions for two pixels where the
    68000's takes five. ST 1,186 -> 1,174ms, TT 105 -> 102.
  - Low detail's pairs on an ST: walls, floors, ceilings and the sky
    draw each pixel twice from an even column, so most of the view is
    pairs of one colour, and those tiles are converted a pair at a time
    (`c2p_pair_lorez` with the 2x table: 22 cycles a pixel against 41,
    the same bits). Anything that draws into the view otherwise says
    where (atari_c2p.h: `atari_unpair_col` in every single-column leaf,
    R_Draw*Column and the masked and translucent posts; `atari_unpair_rect`
    for messages and `atari_fill_rect`; `atari_unpair_view` for the pause,
    stats, damage border, gas mask, HUD location and a frame without
    floors), and for each group of 16 columns those rows go the usual
    way. A low-detail sprite from an even column is drawn as a pair
    (`ScaleMaskedPost2`, the same pixels); the weapon, close and
    translucent sprites, doors and a masked wall's odd edge are single.
    Missing a mark shows that thing's pixels doubled up: an
    `ATARI_PAIR_VERIFY=1` build checks every tile taken as pairs and
    writes the misses to PAIRS.TXT (without the weapon's mark: 200 of
    them). ST 1,174 -> 1,060ms (every tile as pairs would be 1,010).
    In 256 colours the same marks pick the rows STDL's pair conversion
    (`atari_eight_convert_pairs_box`, a box a call) takes: bands of rows
    between the marks' ends, in each a run of 4 groups or more with no mark
    as pairs and the rest as usual (`eight_pairs`). Narrow boxes cost
    STDL's loop more than pairs save: runs of single groups saved nothing,
    and a row at a time (how it was first tried) was slower. The verify
    build checks these boxes too (no faults on the TT; 200 without the
    weapon's mark). TT 2+4MB 78.8 -> 77.5ms, the conversion 20.5 -> 18.3;
    every row as pairs would be 74.6.
    The marks are the rows drawn, not from a column's top to the view's
    bottom: the column routines mark dc_yl to dc_yh, and atari_post_masked
    and _trans say the lowest row they drew (`atari_post_maxrow`), so a
    sprite at the horizon leaves the floor under it in pairs. ST 997 ->
    991ms. The verify builds found no faults on a Mega STE or a TT, in the
    scripted game or a death.
  - The ST's conversion tables again, a colour every 256 bytes
    (`c2p_wide_mem`, 64K, indexed round its middle as d8(An,Xn.w) sign-
    extends): a long of the chunky screen and'ed with $FF00FF00 is two
    indexes as it stands, so the pair loop needs no shift and the 1x
    loop one (lsl.l #8) where it had two. ST 1,033 -> 1,005ms.
  - Arithmetic on a 68000 (atari_cpu.S, checked against the C on random
    and edge inputs by `tools/bench/fixtest.sh`, on Hatari's TT for both
    paths and an STE; broken on purpose, it fails): FixedScale's divisor
    of 17 bits or more (the ray cast's always) is two 16-bit quotient
    digits estimated by divu.w and corrected (Knuth's algorithm D) where
    it was 32 steps of shift and subtract, about 800 cycles against
    1,900; FixedMulShift's shifts other than 16 are `atari_mulshift64`
    (four mulu.w, or the 68030's muls.l) where they were libgcc's
    __muldi3; FixedMul keeps d2-d3 in a0-a1 rather than a movem. ST
    1,060 -> 1,042 and 1,005 -> 1,002ms.
    The ray cast and masked walls (engine.c, rt_draw.c) take FixedMul in
    line (`atari_fixmul.h`): the 68000's sequence in registers, or a
    hand-encoded mulu.l on a 68020 to 68040; fixtest checks it against
    FixedMul. ST 991 -> 986ms, TT 2+4MB 78.3 -> 77.7.
  - Masked posts (`atari_post_masked`, `_masked2`): two spans in three
    of E1L1's start are below the view, found after four mulu.w each. A
    span whose top is past the view's bottom sets a bound in a6, and a
    later span of the post at that offset or lower in the texture is
    skipped with no multiply (exact where the top cannot overflow:
    dc_invscale under 2^22, sprtopoffset under 2^30). ST 1,042 ->
    1,033ms, TT 102.6 -> 101.5. Tried first and dropped: a whole post
    culled when above the view (a walk of its spans for the bound),
    slower on both, as few posts lie wholly outside it.
  - The music player (`atari_music.c`, the firmware's too): a step's
    20ms of song ticks is worked out once a tempo as whole ticks and a
    remainder (it took the tick length away up to a dozen times a step);
    the volume levels scaled by the music volume are a table made again
    when it changes (a mulu.w a voice a step); the tracks are a heap by
    their next event (the earliest at its root, the time after it at its
    children), sifted after the due track's events, where every track
    was scanned for each event. ST 56.6 -> 52.1ms of player a frame.
    `tools/bench/musictest.c` builds the player on the host as the
    firmware does and writes the registers after every step of every
    song in the WAD (18 songs, 15,000 steps each, looping): a change must
    give the same file (a volume a level out does not).
  - Tried and dropped: masked wall pairs noted as jobs and drawn in one
    call a wall (`atari_post_jobs`): noting one cost what the call and
    register save it replaced did (450 cycles on an ST), ST 998 -> 1,022ms.
    `InterpolateMaskedWall` calls atari_post_masked2 and _trans2 itself
    now, a call level less a post.
  - Tried and dropped: low-detail sprites from an odd column started one
    to the left (pairs from an even column): ST 986 -> 985ms, TT the same,
    up to 951 pixels a frame moved. TOS's own time (28ms of an 8MHz ST's
    frame: its VBL and 200Hz handlers' register saves, the floppy VBL's
    delay loop 4ms) was left: taking the VBL over or holding the floppy
    lock through play risks a real drive left selected.
  - Measured and left: the music player's voice update in one pass (each
    pointer and the channel loaded once): the same stream, 51.4 -> 52.0ms,
    gcc had inlined the helpers already; the masked wall spans' mulu.w
    are 8.1% of atari_post_masked2 (about 5ms of an 8MHz ST's frame).
  - Tried and dropped: marks per 8-pixel half of a group, so a group the
    weapon's edge half covers converts its other half as pairs (two movep
    a group, one in pairs): exact, but ST 986 -> 1,012ms, the conversion
    216 -> 237, as the halves break the pair runs up and cost two calls a
    group a row.
  - Tried and dropped: word shading tables for low detail's masked pairs
    (a move.w for two pixels: the loop gained 4.5ms and finding the table
    cost 15.6ms); a faster 68000 __udivsi3 (divu.w itself is the
    cost); a 68030 column loop of three instructions (fraction and row
    in one register, stepped by addx.l), which cannot be exact for
    sprites (a row starting at -1 carries into the fraction where the
    word loop wraps) and saves nothing in low detail's word loop for
    walls; 64K-entry pair tables for the ST's conversion (512K, and two
    seconds at 8MHz to build at each palette change).
  - What is left: on an ST the 16-colour conversion (`c2p_1x_lorez` and
    the pairs, 22% of the frame), the post and wall pixels (the walls'
    loop 38 cycles for two pixels), the music player (5.2% of an 8MHz
    ST's time: its three voices' envelopes and its events), the ray cast
    and the game logic. On a TT the
    8-plane conversion (STDL's, a quarter of the frame, about 12 cycles
    a pixel; its pair form measured no faster) and the pixels. Hatari's TT
    with `--data-cache on` ran the same figures as without.
- `tools/bench` (from the host): `all.sh TAG REF` builds the
  `ATARI_LOGIC_CHECK=4` game and an autotest game linked with symbols
  (`prof.mk`), checks the first on EmuMD's Hatari TT (and a Mega STE with
  `CHECK_MSTE=1`) against REF's run: LOGIC.TXT must be the same file and
  RENDER.TXT (a hash of each frame's chunky screen and of the screen the
  machine shows, `ATARI_CheckRender`) the same frames (`rawdiff.py`, with
  RENDnnn.RAW every 25th frame); then times the second with Hatari's
  profiler (`bench.sh`, machines tt, ttr, st8, mste, falcon) and prints
  the time a frame and where it went (`agg.py`; `-v`, `-i` inclusive,
  `-c FUNC` callers; `hot.py LOG NM FUNC` its instructions). Results in
  tmp/bench.
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

- `ATARI_MD_RENDER=1` (default): `ROTT.TOS` looks for the MD/ROTT
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
- Most of `ATARI_MD_FinishUpdate` runs in supervisor mode on a Mega STE
  (and on a 68030 or later, for its data cache): no
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

SDL build (`ROTT_SDL.TOS`)

- ROTT's own renderer through SDL 1.2's API and MiNTLib, as close to the
  original game as the hardware allows. The SDL is STDL's SDL1 add-on,
  not SDL 1.2 itself (which it used up to atarist.9). `ROTT_SDL.TOS` is
  68030 code for a TT or a Falcon (the add-on's TT and Falcon archive,
  whose `SDL_Init` refuses an ST), soft float so a Falcon needs no 68882:
  compiled `-m68030 -msoft-float`, linked `-m68000` (the toolchain's
  68020-60 multilib is hard float), as STDL's `docs/sdl2.md` shows. The
  68000 build for every machine was dropped: about 1 fps on an ST.
  SDL-only files: `rott/modexlib_sdl.c`, `atari_sdl.c`, `audio_stubs.c`;
  sources are listed in `SDL_SOURCES`, settings are `SDL_*` in the
  Makefile.
- Both kinds of build are MiNT, so `__MINT__` means "any Atari build". In
  shared code, the native builds' hardware and speed code goes under
  `ATARI_NATIVE` (defined by the native build only) and SDL-only code under
  `ATARI_SDL`. Anything else is in both.
- The native `ROTT.TOS` must not change when SDL work is done: build it
  clean before and after and `cmp` (only `STUB_FUNCTION` line numbers may move).
- The SDL build keeps its settings in `sdlconf.rot`/`sdlsound.rot`
  (`rott/sdlconf.rot`, `rott/sdlsound.rot`), so it can share a folder with
  `ROTT.TOS`.
- STDL is the submodule `lib/stdl`, pinned to a release (v1.31.0); it
  carries its own `lib/xpad`, so `git submodule update --init --recursive
  lib/stdl`. The Makefile builds its archives with STDL's own Makefile
  (`libstdl-sdl1.a`, `libstdl-sdl2.a`, `libstdl.a`; stamp `obj/.stdl_libs`),
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
  Nobody had listened to the sound on STDL either. The 68030 figures here
  are the 68882 build's: the soft-float `ROTT_SDL.TOS` that replaced it
  has not been measured.
