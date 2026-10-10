# Makefile for building ROTT for Atari ST, TT & Falcon
#
#   make             the three games below
#   make st          ROTT_ST.TOS: the native ST renderers (C2P, ROTT Accelerator)
#   make sdl         ROTT_SDL.TOS: ROTT's own renderer through SDL, any ST-compatible
#   make sdl-030     ROTT_030.TOS: the same for 68030 + 68882 (TT, Falcon)
#   make sidecart    the ROTT Accelerator firmware (make -C sidecart build)
#
# Run on the host, the games build through stcmd (atarist-toolkit-docker);
# inside stcmd (stcmd make st, ...) they build directly. The firmware builds
# on the host only, with the Pico toolchain (see sidecart/README.md).
#
# Everything lands in dist/, with the game's config files and, when DATADIR
# holds them, the shareware data, so dist/ runs as it is. The firmware puts
# its <uuid>.uf2 and <uuid>.json there too.

# Build settings

SRCDIR ?= rott
DATADIR ?= tmp/ROTT
BUILDDIR ?= dist
OBJDIR ?= obj/atarist

# Debugging

ATARI_DEBUG ?= 0 # Enable debug logging
ATARI_SHOW_FPS ?= 0 # Show FPS overlay

# ROTT Noir?

ATARI_NOIR ?= 0 # Use grayscale palette
ATARI_NOIR_DITHERING ?= 0 # Dither noir output

# SidecarTridge Multi-device renderer (MD/ROTT, see sidecart/)

ATARI_MD_RENDER ?= 1 # Use the MD/ROTT firmware when present
ATARI_MD_PIPELINE ?= 1 # MD renders frame N while the ST runs N+1
ATARI_MD_BLIT ?= 1 # The blitter copies MD frames while the CPU goes on
ATARI_MD_AUTOTEST ?= 0 # Test runs: start a game, turn N angles a tic
ATARI_MD_AUTOTEST_DIE ?= 0 # Test runs: the player is killed at frame N
ATARI_LOGIC_CHECK ?= 0 # Test runs: N tics a frame, scripted, state hashes

# Rendering and performance settings

ATARI_ACTOR_BUDGET ?= 0 # Max actor updates
ATARI_ACTOR_THROTTLE_DIV ?= 3 # Actor update divisor
ATARI_C2P_DIRTY_TILES ?= 1 # Update only dirty tiles
ATARI_C2P_DIRTY_TILE_THRESHOLD ?= 200 # Dirty-tile cutoff
ATARI_C2P_FAST_COPY ?= 1 # Use fast C2P copy
ATARI_C2P_STRICT_NO_OVERLAP ?= 1 # Prevent HUD overlap
ATARI_C2P_VIEW_ZOOM ?= 1 # Auto zoom viewport
ATARI_CATCHUP_POLL_INTERVAL ?= 4 # Input poll interval
ATARI_EFFECT_BUDGET ?= 0 # Max effect passes
ATARI_ENABLE_KBDINT ?= 1 # Use keyboard interrupt
ATARI_FADE_SCALE ?= 4 # Fade speed scale
ATARI_FLAT_WALL_LIGHT ?= 1 # Flatten wall lighting
ATARI_FLAT_WORLD ?= 0 # Disable floors and ceilings
ATARI_HIDE_WEAPON ?= 0 # Hide weapon sprite
ATARI_MAX_CATCHUP_STEPS ?= 0 # Max logic catch-up steps
ATARI_MAX_RAY_STEPS ?= 24 # Max ray steps
ATARI_MIN_SPRITE_HEIGHT ?= 256 # Min sprite draw height
ATARI_PROFILE ?= 0 # Enable perf profiling
ATARI_SHAREWARE ?= 1 # Build shareware data
ATARI_SITELICENSE ?= 0 # Build site license data
ATARI_SKIP_PRECACHE ?= 1 # Skip startup precache
ATARI_SKIP_FADES ?= 1 # Skip fade effects
ATARI_SKIP_LIGHTLEVEL ?= 1 # Skip lightlevel setup
ATARI_SKIP_FIZZLE ?= 1 # Skip fizzle transition
ATARI_SPRITE_BUDGET ?= 0 # Max sprite draws
ATARI_SUPERROTT ?= 0 # Build Super ROTT data
ATARI_USE_ASM_HOTSPOTS ?= 0 # Enable asm hotspots
ATARI_VIEW_SCALE_DIV ?= 1 # Divide view resolution
ATARI_WALL_ANIM_DIVISOR ?= 2 # Wall animation divisor

# Compiler and linker settings

ATARI_CC ?= m68k-atari-mint-gcc
ATARI_CFLAGS ?= -O3 -fomit-frame-pointer -s -std=gnu99 -m68000 \
	-fno-strict-aliasing -DPLATFORM_TIMER_HZ=200 -DATARI_NATIVE=1 \
		-DSHAREWARE=$(ATARI_SHAREWARE) \
	-DSUPERROTT=$(ATARI_SUPERROTT) -DSITELICENSE=$(ATARI_SITELICENSE) \
	-DATARI_ENABLE_KBDINT=$(ATARI_ENABLE_KBDINT) \
	-DATARI_SKIP_PRECACHE=$(ATARI_SKIP_PRECACHE) -DATARI_SKIP_FADES=$(ATARI_SKIP_FADES) \
	-DATARI_FADE_SCALE=$(ATARI_FADE_SCALE) \
	-DATARI_DEBUG=$(ATARI_DEBUG) -DATARI_SHOW_FPS=$(ATARI_SHOW_FPS) \
	-DATARI_C2P_VIEW_ZOOM=$(ATARI_C2P_VIEW_ZOOM) \
	-DATARI_C2P_STRICT_NO_OVERLAP=$(ATARI_C2P_STRICT_NO_OVERLAP) \
	-DATARI_NOIR=$(ATARI_NOIR) \
	-DATARI_NOIR_DITHERING=$(ATARI_NOIR_DITHERING) \
	-DATARI_C2P_FAST_COPY=$(ATARI_C2P_FAST_COPY) \
	-DATARI_C2P_DIRTY_TILES=$(ATARI_C2P_DIRTY_TILES) \
	-DATARI_C2P_DIRTY_TILE_THRESHOLD=$(ATARI_C2P_DIRTY_TILE_THRESHOLD) \
	-DATARI_MAX_CATCHUP_STEPS=$(ATARI_MAX_CATCHUP_STEPS) \
	-DATARI_CATCHUP_POLL_INTERVAL=$(ATARI_CATCHUP_POLL_INTERVAL) \
	-DATARI_ACTOR_THROTTLE_DIV=$(ATARI_ACTOR_THROTTLE_DIV) \
	-DATARI_WALL_ANIM_DIVISOR=$(ATARI_WALL_ANIM_DIVISOR) \
	-DATARI_MAX_RAY_STEPS=$(ATARI_MAX_RAY_STEPS) \
	-DATARI_MIN_SPRITE_HEIGHT=$(ATARI_MIN_SPRITE_HEIGHT) \
	-DATARI_PROFILE=$(ATARI_PROFILE) \
	-DATARI_ACTOR_BUDGET=$(ATARI_ACTOR_BUDGET) \
	-DATARI_SPRITE_BUDGET=$(ATARI_SPRITE_BUDGET) \
	-DATARI_EFFECT_BUDGET=$(ATARI_EFFECT_BUDGET) \
	-DATARI_FLAT_WORLD=$(ATARI_FLAT_WORLD) \
	-DATARI_VIEW_SCALE_DIV=$(ATARI_VIEW_SCALE_DIV) \
	-DATARI_HIDE_WEAPON=$(ATARI_HIDE_WEAPON) \
	-DATARI_FLAT_WALL_LIGHT=$(ATARI_FLAT_WALL_LIGHT) \
	-DATARI_USE_ASM_HOTSPOTS=$(ATARI_USE_ASM_HOTSPOTS) \
	-DATARI_SKIP_LIGHTLEVEL=$(ATARI_SKIP_LIGHTLEVEL) -DATARI_SKIP_FIZZLE=$(ATARI_SKIP_FIZZLE) \
	-DATARI_MD_RENDER=$(ATARI_MD_RENDER) -DATARI_MD_PIPELINE=$(ATARI_MD_PIPELINE) \
	-DATARI_MD_BLIT=$(ATARI_MD_BLIT) \
	-DATARI_MD_AUTOTEST=$(ATARI_MD_AUTOTEST) -DATARI_LOGIC_CHECK=$(ATARI_LOGIC_CHECK) \
	-DATARI_MD_AUTOTEST_DIE=$(ATARI_MD_AUTOTEST_DIE)
ATARI_LDFLAGS ?= -s -nostdlib -L/freemint/libcmini/lib /freemint/libcmini/lib/crt0.o -m68000
ATARI_LIBS ?= -lcmini -lgcc
ATARI_INCLUDES ?= -I$(SRCDIR) -I$(SRCDIR)/audiolib -Isidecart/include -Ilib/xpad/src -I/freemint/libcmini/include
SDL_ONLY_SOURCES := $(SRCDIR)/atari_sdl.c $(SRCDIR)/audio_stubs.c $(SRCDIR)/modexlib_sdl.c
# Sound effects are rott/atari_sfx.c (the FX_ calls, on DMA sound), in place
# of the DOS library's fx_man.c, multivoc.c and its DMA driver.
ATARI_AUDIOLIB_SOURCES := \
	$(SRCDIR)/audiolib/atari_stubs.c \
	$(SRCDIR)/audiolib/atari_music.c \
	$(SRCDIR)/audiolib/atari_music_api.c \
	$(SRCDIR)/audiolib/debugio.c \
	$(SRCDIR)/audiolib/ll_man.c \
	$(SRCDIR)/audiolib/nodpmi.c \
	$(SRCDIR)/audiolib/pitch.c \
	$(SRCDIR)/audiolib/user.c \
	$(SRCDIR)/audiolib/usrhooks.c
ATARI_SOURCES := $(filter-out $(SRCDIR)/amiga_%.c $(SRCDIR)/dosutil.c $(SRCDIR)/dukemusc.c $(SRCDIR)/fx_man.c $(SRCDIR)/lookups.c $(SRCDIR)/vocdecode.c $(SDL_ONLY_SOURCES),$(wildcard $(SRCDIR)/*.c)) $(ATARI_AUDIOLIB_SOURCES) \
	lib/xpad/src/xpad.c
ATARI_ASM_SOURCES := $(SRCDIR)/sidecart_stubs.S $(SRCDIR)/atari_md_s.S
ATARI_OBJECTS := $(addprefix $(OBJDIR)/,$(ATARI_SOURCES:.c=.o) $(ATARI_ASM_SOURCES:.S=.o))
ATARI_OUTPUT ?= $(BUILDDIR)/ROTT_ST.TOS
RUNTIME_DATA_FILES := \
	$(DATADIR)/HUNTBGIN.WAD \
	$(DATADIR)/HUNTBGIN.RTL \
	$(DATADIR)/HUNTBGIN.RTC \
	$(DATADIR)/REMOTE1.RTS
RUNTIME_CONFIG_FILES := \
	$(SRCDIR)/battle.rot \
	$(SRCDIR)/scores.rot
ATARI_RUNTIME_CONFIG_FILES := \
	$(SRCDIR)/config.rot \
	$(SRCDIR)/sound.rot
ATARI_FLAGS_STAMP := $(OBJDIR)/.atari_flags

# Build targets

# With the m68k toolchain at hand (inside stcmd), the games build directly.
# Without it (on the host) they build through stcmd, passing on ATARI_*,
# SDL_*, BUILDDIR, OBJDIR and DATADIR; JOBS is its -j.
TOOLCHAIN := $(shell command -v $(ATARI_CC) 2>/dev/null)
JOBS ?= 4
STCMD_MAKE = STCMD_NO_TTY=1 STCMD_QUIET=1 stcmd make -j$(JOBS) \
	$(filter ATARI_% SDL_% BUILDDIR=% OBJDIR=% DATADIR=%,$(MAKEOVERRIDES))

ifneq ($(TOOLCHAIN),)
games: st sdl sdl-030
st: rott-huntbgin
else
games:
	$(STCMD_MAKE) st sdl sdl-030
st sdl sdl-030:
	$(STCMD_MAKE) $@
endif

sidecart:
	$(MAKE) -C sidecart build

.PHONY: games st sdl sdl-030 sidecart
.PHONY: FORCE rott-huntbgin rott-darkwar rott-rottcd rott-rottsite rott-dev rott-68882 stage-runtime-files atari-stage-runtime-files
.SECONDARY: $(ATARI_OBJECTS)
FORCE:

# Shareware build (default)
rott-huntbgin: ATARI_SHAREWARE = 1
rott-huntbgin: ATARI_SUPERROTT = 0
rott-huntbgin: ATARI_SITELICENSE = 0
rott-huntbgin: atari-stage-runtime-files $(ATARI_OUTPUT)

# Shareware build with 68882 FPU code generation
rott-68882: ATARI_LDFLAGS += -m68882
rott-68882: rott-huntbgin

# Shareware build (experimental dev settings)
rott-dev: ATARI_ACTOR_THROTTLE_DIV = 3
rott-dev: ATARI_MAX_RAY_STEPS = 20
rott-dev: ATARI_MIN_SPRITE_HEIGHT = 1024
rott-dev: ATARI_ACTOR_BUDGET = 96
rott-dev: ATARI_SPRITE_BUDGET = 96
rott-dev: ATARI_EFFECT_BUDGET = 1
rott-dev: ATARI_FLAT_WORLD = 1
rott-dev: ATARI_VIEW_SCALE_DIV = 2
rott-dev: ATARI_HIDE_WEAPON = 1
rott-dev: ATARI_FLAT_WALL_LIGHT = 1
rott-dev: ATARI_SHOW_FPS = 1
rott-dev: rott-huntbgin

# Commercial build
rott-darkwar: ATARI_SHAREWARE = 0
rott-darkwar: ATARI_SUPERROTT = 0
rott-darkwar: ATARI_SITELICENSE = 0
rott-darkwar: atari-stage-runtime-files $(ATARI_OUTPUT)

# CD build
rott-rottcd: ATARI_SHAREWARE = 0
rott-rottcd: ATARI_SUPERROTT = 0
rott-rottcd: ATARI_SITELICENSE = 0
rott-rottcd: atari-stage-runtime-files $(ATARI_OUTPUT)

# Site license build
rott-rottsite: ATARI_SHAREWARE = 0
rott-rottsite: ATARI_SUPERROTT = 0
rott-rottsite: ATARI_SITELICENSE = 1
rott-rottsite: atari-stage-runtime-files $(ATARI_OUTPUT)

# SDL builds: ROTT's own renderer through SDL 1.2 and MiNTLib, as close to
# the original game as the hardware allows (from the sdl branch). Shared code
# tells the two kinds of build apart with ATARI_NATIVE (the native renderers'
# hardware code) and ATARI_SDL. They keep their settings in sdlconf.rot and
# sdlsound.rot, so they can sit beside ROTT_ST.TOS without upsetting it.

SDL_OBJDIR ?= obj/sdl
SDL_030_OBJDIR ?= obj/sdl-030

SDL_SHOW_FPS ?= 0 # Show FPS overlay
SDL_SKIP_FADES ?= 0 # Skip fade effects
SDL_SKIP_FIZZLE ?= 0 # Skip fizzle transition
SDL_SKIP_LIGHTLEVEL ?= 0 # Skip lightlevel setup
SDL_TARGET_FPS ?= 12 # Frame rate cap
SDL_TIC_WAIT_MS ?= 0 # Sleep while waiting for the next tic

SDL_CC ?= m68k-atari-mint-gcc
SDL_CFLAGS ?= -I/usr/m68k-atari-mint/include/SDL -D_GNU_SOURCE=1
SDL_LIBS ?= -lSDL -lgem -lldg -lgem -lm
SDL_GAME_CFLAGS := -O3 -fomit-frame-pointer -fno-strict-aliasing -ffast-math -std=gnu99 \
	-DPLATFORM_UNIX=1 -DATARI_SDL=1 -DC_FIXED_MATH=1 -DUSE_SDL=0 \
	-DSHAREWARE=$(ATARI_SHAREWARE) -DSUPERROTT=$(ATARI_SUPERROTT) \
	-DSITELICENSE=$(ATARI_SITELICENSE) \
	-DATARI_SHOW_FPS=$(SDL_SHOW_FPS) -DATARI_SKIP_FADES=$(SDL_SKIP_FADES) \
	-DATARI_SKIP_FIZZLE=$(SDL_SKIP_FIZZLE) -DATARI_SKIP_LIGHTLEVEL=$(SDL_SKIP_LIGHTLEVEL) \
	-DATARI_TARGET_FPS=$(SDL_TARGET_FPS) -DATARI_TIC_WAIT_MS=$(SDL_TIC_WAIT_MS) \
	-I$(SRCDIR) -I$(SRCDIR)/audiolib -Isidecart/include $(SDL_CFLAGS)
SDL_SOURCES := $(addprefix $(SRCDIR)/, \
	atari_megaste.c atari_sdl.c audio_stubs.c byteordr.c cin_actr.c cin_efct.c \
	cin_evnt.c cin_glob.c cin_main.c cin_util.c dosutil.c engine.c i_timer.c isr.c \
	modexlib_sdl.c rt_actor.c rt_battl.c rt_build.c rt_cfg.c rt_com.c rt_crc.c \
	rt_debug.c rt_dmand.c rt_door.c rt_draw.c rt_err.c rt_floor.c rt_game.c rt_in.c \
	rt_main.c rt_map.c rt_menu.c rt_msg.c rt_net.c rt_playr.c rt_rand.c rt_scale.c \
	rt_sound.c rt_spbal.c rt_sqrt.c rt_stat.c rt_state.c rt_str.c rt_swift.c \
	rt_ted.c rt_util.c rt_vid.c rt_view.c scriplib.c w_wad.c watcom.c winrott.c \
	z_zone.c)
SDL_OBJECTS := $(addprefix $(SDL_OBJDIR)/,$(SDL_SOURCES:.c=.o))
SDL_030_OBJECTS := $(addprefix $(SDL_030_OBJDIR)/,$(SDL_SOURCES:.c=.o))
SDL_OUTPUT ?= $(BUILDDIR)/ROTT_SDL.TOS
SDL_030_OUTPUT ?= $(BUILDDIR)/ROTT_030.TOS
SDL_RUNTIME_CONFIG_FILES := \
	$(SRCDIR)/sdlconf.rot \
	$(SRCDIR)/sdlsound.rot

.PHONY: sdl-stage-runtime-files
.SECONDARY: $(SDL_OBJECTS) $(SDL_030_OBJECTS)

ifneq ($(TOOLCHAIN),)
sdl: sdl-stage-runtime-files $(SDL_OUTPUT)
sdl-030: sdl-stage-runtime-files $(SDL_030_OUTPUT)
endif

clean:
	$(RM) -r $(ATARI_OUTPUT) $(SDL_OUTPUT) $(SDL_030_OUTPUT) $(OBJDIR) $(SDL_OBJDIR) $(SDL_030_OBJDIR)

# The game in EmuMD (sidecart/emu/emumd): Hatari with the ROTT Accelerator
# emulated on the cartridge port. Run on the host, not in stcmd: builds
# ROTT_ST.TOS (make st, passing on any ATARI_*, BUILDDIR or OBJDIR given
# here) and the firmware for the host, then starts Hatari. EMU_ARGS go to
# mdfw run:
#   make emu ATARI_SHOW_FPS=1
#   make emu EMU_ARGS="--headless --frames 3000 --screenshot out.png"
# The first time, build EmuMD's Hatari: sidecart/emu/emumd/tools/mdfw hatari
EMU_SD ?= tmp/sd
EMU_WAD ?= HUNTBGIN.WAD
EMU_ARGS ?=

.PHONY: emu
emu: st
	@[ -x sidecart/emu/emumd/tools/mdfw ] || git submodule update --init sidecart/emu/emumd
	@mkdir -p $(EMU_SD)/rott
	@[ -e $(EMU_SD)/rott/$(EMU_WAD) ] || cp $(DATADIR)/$(EMU_WAD) $(EMU_SD)/rott/
	cd sidecart && emu/emumd/tools/mdfw run --harddrive $(abspath $(BUILDDIR)) \
		--sd $(abspath $(EMU_SD)) $(EMU_ARGS)

$(ATARI_OUTPUT): $(ATARI_OBJECTS) | $(BUILDDIR)
	$(ATARI_CC) $(ATARI_LDFLAGS) $(ATARI_OBJECTS) $(ATARI_LIBS) -o $@

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

# Copy the config files given into BUILDDIR.
define STAGE_CONFIG_FILES
	@for src in $(1); do \
		dst="$(BUILDDIR)/$$(basename "$$src")"; \
		if [ -e "$$src" ]; then \
			cp -f "$$src" "$$dst"; \
		fi; \
	done
endef

# What every build needs beside it: the shared config files, then the game
# data when DATADIR has it. Each file has one target copying it, so a
# parallel build of several games never copies the same file twice at once.
stage-runtime-files: | $(BUILDDIR)
	$(call STAGE_CONFIG_FILES,$(RUNTIME_CONFIG_FILES))
	@if [ -d "$(DATADIR)" ]; then \
		for src in $(RUNTIME_DATA_FILES); do \
			dst="$(BUILDDIR)/$$(basename "$$src")"; \
			if [ -e "$$src" ] && [ ! -e "$$dst" ]; then \
				cp "$$src" "$$dst"; \
			fi; \
		done; \
	fi

atari-stage-runtime-files: stage-runtime-files
	@cp -f lib/xpad/LICENSE $(BUILDDIR)/XPAD.TXT
	$(call STAGE_CONFIG_FILES,$(ATARI_RUNTIME_CONFIG_FILES))

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(ATARI_FLAGS_STAMP): FORCE | $(OBJDIR)
	@tmp="$@.tmp"; \
	{ \
		echo "ATARI_CC=$(ATARI_CC)"; \
		echo "ATARI_CFLAGS=$(ATARI_CFLAGS)"; \
		echo "ATARI_INCLUDES=$(ATARI_INCLUDES)"; \
	} > "$$tmp"; \
	if [ -f "$@" ] && cmp -s "$$tmp" "$@"; then \
		rm -f "$$tmp"; \
	else \
		mv "$$tmp" "$@"; \
	fi

$(OBJDIR)/%.o: %.c $(ATARI_FLAGS_STAMP)
	mkdir -p $(dir $@)
	$(ATARI_CC) $(ATARI_CFLAGS) $(ATARI_INCLUDES) -c $< -o $@

$(OBJDIR)/%.o: %.S $(ATARI_FLAGS_STAMP)
	mkdir -p $(dir $@)
	$(ATARI_CC) -m68000 -c $< -o $@

sdl-stage-runtime-files: stage-runtime-files
	$(call STAGE_CONFIG_FILES,$(SDL_RUNTIME_CONFIG_FILES))

$(SDL_OUTPUT): $(SDL_OBJECTS) | $(BUILDDIR)
	$(SDL_CC) -s -m68000 $(SDL_OBJECTS) $(SDL_LIBS) -o $@

$(SDL_030_OUTPUT): $(SDL_030_OBJECTS) | $(BUILDDIR)
	$(SDL_CC) -s -m68030 -m68882 $(SDL_030_OBJECTS) $(SDL_LIBS) -o $@

# As ATARI_FLAGS_STAMP: objects rebuild when the flags change.
$(SDL_OBJDIR)/.sdl_flags: SDL_STAMP = $(SDL_CC) -m68000 $(SDL_GAME_CFLAGS)
$(SDL_030_OBJDIR)/.sdl_flags: SDL_STAMP = $(SDL_CC) -m68030 $(SDL_GAME_CFLAGS)
$(SDL_OBJDIR)/.sdl_flags $(SDL_030_OBJDIR)/.sdl_flags: FORCE
	@mkdir -p $(@D)
	@echo "$(SDL_STAMP)" > "$@.tmp"; \
	if [ -f "$@" ] && cmp -s "$@.tmp" "$@"; then rm -f "$@.tmp"; else mv "$@.tmp" "$@"; fi

$(SDL_OBJDIR)/%.o: %.c $(SDL_OBJDIR)/.sdl_flags
	@mkdir -p $(dir $@)
	$(SDL_CC) -m68000 $(SDL_GAME_CFLAGS) -MMD -MP -c $< -o $@

$(SDL_030_OBJDIR)/%.o: %.c $(SDL_030_OBJDIR)/.sdl_flags
	@mkdir -p $(dir $@)
	$(SDL_CC) -m68030 $(SDL_GAME_CFLAGS) -MMD -MP -c $< -o $@

-include $(SDL_OBJECTS:.o=.d) $(SDL_030_OBJECTS:.o=.d)
