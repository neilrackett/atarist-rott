# Makefile for building ROTT for Atari ST

# Build settings

SRCDIR ?= rott
DATADIR ?= tmp/ROTT
BUILDDIR ?= build/atari
OBJDIR ?= obj/atari

# Debugging

ATARI_DEBUG ?= 0 # Enable debug logging
ATARI_SHOW_FPS ?= 1 # Show FPS overlay

# ROTT Noir?

ATARI_NOIR ?= 0 # Use grayscale palette
ATARI_NOIR_DITHERING ?= 0 # Dither noir output

# Rendering and performance settings

ATARI_ACTOR_BUDGET ?= 0 # Max actor updates
ATARI_ACTOR_THROTTLE_DIV ?= 3 # Actor update divisor
ATARI_ACTOR_THROTTLE_MAX_DIV ?= 5 # Max actor throttle
ATARI_ADAPTIVE_ACTOR_THROTTLE ?= 1 # Auto throttle actors
ATARI_ADAPTIVE_RENDER ?= 1 # Auto adjust render rate
ATARI_ADAPTIVE_RENDER_DOWN_FRAMES ?= 10 # Frames before speedup
ATARI_ADAPTIVE_RENDER_MAX_DIV ?= 4 # Max render divisor
ATARI_ADAPTIVE_RENDER_UP_TICS ?= 2 # Tics before slowdown
ATARI_C2P_DIRTY_TILES ?= 1 # Update only dirty tiles
ATARI_C2P_DIRTY_TILE_THRESHOLD ?= 200 # Dirty-tile cutoff
ATARI_C2P_FAST_COPY ?= 1 # Use fast C2P copy
ATARI_C2P_STRICT_NO_OVERLAP ?= 1 # Prevent HUD overlap
ATARI_C2P_VIEW_ZOOM ?= 1 # Auto zoom viewport
ATARI_CATCHUP_POLL_INTERVAL ?= 4 # Input poll interval
ATARI_DYNAMIC_QUALITY ?= 1 # Enable dynamic quality
ATARI_DYNAMIC_QUALITY_DOWN_FRAMES ?= 12 # Frames before quality drop
ATARI_DYNAMIC_QUALITY_MAX ?= 3 # Max quality level
ATARI_DYNAMIC_QUALITY_UP_TICS ?= 1 # Tics before quality raise
ATARI_EFFECT_BUDGET ?= 0 # Max effect passes
ATARI_ENABLE_BLITTER ?= 1 # Use Atari blitter
ATARI_ENABLE_KBDINT ?= 1 # Use keyboard interrupt
ATARI_FADE_SCALE ?= 4 # Fade speed scale
ATARI_FLAT_WALL_LIGHT ?= 0 # Flatten wall lighting
ATARI_FLAT_WORLD ?= 0 # Disable floors and ceilings
ATARI_HIDE_WEAPON ?= 0 # Hide weapon sprite
ATARI_LOWP ?= 1 # Enable low precision mode
ATARI_LOWP_ANGLE_SHIFT ?= 1 # Angle precision shift
ATARI_LOWP_HEIGHT_SHIFT ?= 1 # Height precision shift
ATARI_LOWP_TEXTURE_SHIFT ?= 2 # Texture precision shift
ATARI_MAX_CATCHUP_STEPS ?= 0 # Max logic catch-up steps
ATARI_MAX_RAY_STEPS ?= 24 # Max ray steps
ATARI_MENU_CURSOR_DELAY_TICS ?= 0 # Cursor animation delay
ATARI_MENU_EVENT_DRIVEN ?= 1 # Event-driven menu loop
ATARI_MIN_SPRITE_HEIGHT ?= 768 # Min sprite draw height
ATARI_PROFILE ?= 0 # Enable perf profiling
ATARI_RAYCAST_STEP ?= 4 # Base ray step size
ATARI_RENDER_DIVISOR ?= 1 # Frame render divisor
ATARI_SHAREWARE ?= 1 # Build shareware data
ATARI_SITELICENSE ?= 0 # Build site license data
ATARI_SKIP_PRECACHE ?= 1 # Skip startup precache
ATARI_SKIP_FADES ?= 1 # Skip fade effects
ATARI_SKIP_LIGHTLEVEL ?= 1 # Skip lightlevel setup
ATARI_SKIP_FIZZLE ?= 1 # Skip fizzle transition
ATARI_SPRITE_BUDGET ?= 0 # Max sprite draws
ATARI_SPRITE_FRAME_DIV ?= 1 # Sprite frame skip
ATARI_SUPERROTT ?= 0 # Build Super ROTT data
ATARI_TARGET_FPS ?= 5 # Target render FPS
ATARI_USE_ASM_HOTSPOTS ?= 0 # Enable asm hotspots
ATARI_VIEW_SCALE_DIV ?= 1 # Divide view resolution
ATARI_WALL_ANIM_DIVISOR ?= 2 # Wall animation divisor

# Compiler and linker settings

ATARI_CC ?= m68k-atari-mint-gcc
ATARI_CFLAGS ?= -O2 -fomit-frame-pointer -s -std=gnu99 -m68000 \
	-fno-strict-aliasing -DPLATFORM_TIMER_HZ=200 \
		-DSHAREWARE=$(ATARI_SHAREWARE) \
	-DSUPERROTT=$(ATARI_SUPERROTT) -DSITELICENSE=$(ATARI_SITELICENSE) \
	-DATARI_ENABLE_KBDINT=$(ATARI_ENABLE_KBDINT) \
	-DATARI_ENABLE_BLITTER=$(ATARI_ENABLE_BLITTER) \
	-DATARI_SKIP_PRECACHE=$(ATARI_SKIP_PRECACHE) -DATARI_SKIP_FADES=$(ATARI_SKIP_FADES) \
	-DATARI_FADE_SCALE=$(ATARI_FADE_SCALE) \
	-DATARI_DEBUG=$(ATARI_DEBUG) -DATARI_SHOW_FPS=$(ATARI_SHOW_FPS) \
	-DATARI_TARGET_FPS=$(ATARI_TARGET_FPS) \
	-DATARI_C2P_VIEW_ZOOM=$(ATARI_C2P_VIEW_ZOOM) \
	-DATARI_C2P_STRICT_NO_OVERLAP=$(ATARI_C2P_STRICT_NO_OVERLAP) \
	-DATARI_NOIR=$(ATARI_NOIR) \
	-DATARI_NOIR_DITHERING=$(ATARI_NOIR_DITHERING) \
	-DATARI_C2P_FAST_COPY=$(ATARI_C2P_FAST_COPY) \
	-DATARI_C2P_DIRTY_TILES=$(ATARI_C2P_DIRTY_TILES) \
	-DATARI_C2P_DIRTY_TILE_THRESHOLD=$(ATARI_C2P_DIRTY_TILE_THRESHOLD) \
	-DATARI_MAX_CATCHUP_STEPS=$(ATARI_MAX_CATCHUP_STEPS) \
	-DATARI_CATCHUP_POLL_INTERVAL=$(ATARI_CATCHUP_POLL_INTERVAL) \
	-DATARI_RENDER_DIVISOR=$(ATARI_RENDER_DIVISOR) \
	-DATARI_ACTOR_THROTTLE_DIV=$(ATARI_ACTOR_THROTTLE_DIV) \
	-DATARI_ADAPTIVE_RENDER=$(ATARI_ADAPTIVE_RENDER) \
	-DATARI_ADAPTIVE_RENDER_MAX_DIV=$(ATARI_ADAPTIVE_RENDER_MAX_DIV) \
	-DATARI_ADAPTIVE_RENDER_UP_TICS=$(ATARI_ADAPTIVE_RENDER_UP_TICS) \
	-DATARI_ADAPTIVE_RENDER_DOWN_FRAMES=$(ATARI_ADAPTIVE_RENDER_DOWN_FRAMES) \
	-DATARI_ADAPTIVE_ACTOR_THROTTLE=$(ATARI_ADAPTIVE_ACTOR_THROTTLE) \
	-DATARI_ACTOR_THROTTLE_MAX_DIV=$(ATARI_ACTOR_THROTTLE_MAX_DIV) \
	-DATARI_WALL_ANIM_DIVISOR=$(ATARI_WALL_ANIM_DIVISOR) \
	-DATARI_MAX_RAY_STEPS=$(ATARI_MAX_RAY_STEPS) \
	-DATARI_MIN_SPRITE_HEIGHT=$(ATARI_MIN_SPRITE_HEIGHT) \
	-DATARI_PROFILE=$(ATARI_PROFILE) \
	-DATARI_DYNAMIC_QUALITY=$(ATARI_DYNAMIC_QUALITY) \
	-DATARI_DYNAMIC_QUALITY_MAX=$(ATARI_DYNAMIC_QUALITY_MAX) \
	-DATARI_DYNAMIC_QUALITY_UP_TICS=$(ATARI_DYNAMIC_QUALITY_UP_TICS) \
	-DATARI_DYNAMIC_QUALITY_DOWN_FRAMES=$(ATARI_DYNAMIC_QUALITY_DOWN_FRAMES) \
	-DATARI_LOWP=$(ATARI_LOWP) \
	-DATARI_LOWP_TEXTURE_SHIFT=$(ATARI_LOWP_TEXTURE_SHIFT) \
	-DATARI_LOWP_HEIGHT_SHIFT=$(ATARI_LOWP_HEIGHT_SHIFT) \
	-DATARI_LOWP_ANGLE_SHIFT=$(ATARI_LOWP_ANGLE_SHIFT) \
	-DATARI_RAYCAST_STEP=$(ATARI_RAYCAST_STEP) \
	-DATARI_ACTOR_BUDGET=$(ATARI_ACTOR_BUDGET) \
	-DATARI_SPRITE_BUDGET=$(ATARI_SPRITE_BUDGET) \
	-DATARI_EFFECT_BUDGET=$(ATARI_EFFECT_BUDGET) \
	-DATARI_FLAT_WORLD=$(ATARI_FLAT_WORLD) \
	-DATARI_VIEW_SCALE_DIV=$(ATARI_VIEW_SCALE_DIV) \
	-DATARI_SPRITE_FRAME_DIV=$(ATARI_SPRITE_FRAME_DIV) \
	-DATARI_HIDE_WEAPON=$(ATARI_HIDE_WEAPON) \
	-DATARI_FLAT_WALL_LIGHT=$(ATARI_FLAT_WALL_LIGHT) \
	-DATARI_USE_ASM_HOTSPOTS=$(ATARI_USE_ASM_HOTSPOTS) \
	-DATARI_MENU_CURSOR_DELAY_TICS=$(ATARI_MENU_CURSOR_DELAY_TICS) \
	-DATARI_MENU_EVENT_DRIVEN=$(ATARI_MENU_EVENT_DRIVEN) \
	-DATARI_SKIP_LIGHTLEVEL=$(ATARI_SKIP_LIGHTLEVEL) -DATARI_SKIP_FIZZLE=$(ATARI_SKIP_FIZZLE)
ATARI_LDFLAGS ?= -s -nostdlib -L/freemint/libcmini/lib /freemint/libcmini/lib/crt0.o -m68000
ATARI_LIBS ?= -lcmini -lgcc
ATARI_INCLUDES ?= -I$(SRCDIR) -I$(SRCDIR)/audiolib -I/freemint/libcmini/include
ATARI_AUDIOLIB_SOURCES := \
	$(SRCDIR)/audiolib/atari_stubs.c \
	$(SRCDIR)/audiolib/atari_music.c \
	$(SRCDIR)/audiolib/atari_music_api.c \
	$(SRCDIR)/audiolib/debugio.c \
	$(SRCDIR)/audiolib/dsl.c \
	$(SRCDIR)/audiolib/fx_man.c \
	$(SRCDIR)/audiolib/ll_man.c \
	$(SRCDIR)/audiolib/multivoc.c \
	$(SRCDIR)/audiolib/mv_mix.c \
	$(SRCDIR)/audiolib/mvreverb.c \
	$(SRCDIR)/audiolib/nodpmi.c \
	$(SRCDIR)/audiolib/pitch.c \
	$(SRCDIR)/audiolib/user.c \
	$(SRCDIR)/audiolib/usrhooks.c
ATARI_SOURCES := $(filter-out $(SRCDIR)/amiga_%.c $(SRCDIR)/dosutil.c $(SRCDIR)/dukemusc.c $(SRCDIR)/fx_man.c $(SRCDIR)/lookups.c $(SRCDIR)/vocdecode.c,$(wildcard $(SRCDIR)/*.c)) $(ATARI_AUDIOLIB_SOURCES)
ATARI_OBJECTS := $(addprefix $(OBJDIR)/,$(ATARI_SOURCES:.c=.o))
ATARI_OUTPUT_SHAREWARE ?= $(BUILDDIR)/ROTT_ST.TOS
ATARI_OUTPUT_DARKWAR ?= $(BUILDDIR)/ROTT_STD.TOS
ATARI_OUTPUT_ROTTCD ?= $(BUILDDIR)/ROTT_STC.TOS
ATARI_OUTPUT_ROTTSITE ?= $(BUILDDIR)/ROTT_STS.TOS
ATARI_OUTPUTS := $(ATARI_OUTPUT_SHAREWARE) $(ATARI_OUTPUT_DARKWAR) $(ATARI_OUTPUT_ROTTCD) $(ATARI_OUTPUT_ROTTSITE)
ATARI_RUNTIME_DATA_FILES := \
	$(DATADIR)/HUNTBGIN.WAD \
	$(DATADIR)/HUNTBGIN.RTL \
	$(DATADIR)/HUNTBGIN.RTC \
	$(DATADIR)/REMOTE1.RTS
ATARI_RUNTIME_CONFIG_FILES := \
	$(SRCDIR)/config.rot \
	$(SRCDIR)/sound.rot \
	$(SRCDIR)/battle.rot \
	$(SRCDIR)/scores.rot
ATARI_FLAGS_STAMP := $(OBJDIR)/.atari_flags

# Build targets

all: rott-huntbgin
.PHONY: FORCE rott-huntbgin rott-darkwar rott-rottcd rott-rottsite rott-dev rott-68882 atari-stage-runtime-files
.SECONDARY: $(ATARI_OBJECTS)
FORCE:

# Shareware build (default)
rott-huntbgin: ATARI_SHAREWARE = 1
rott-huntbgin: ATARI_SUPERROTT = 0
rott-huntbgin: ATARI_SITELICENSE = 0
rott-huntbgin: atari-stage-runtime-files $(ATARI_OUTPUT_SHAREWARE)

# Shareware build with 68882 FPU code generation
rott-68882: ATARI_LDFLAGS += -m68882
rott-68882: rott-huntbgin

# Shareware build (experimental dev settings)
rott-dev: ATARI_TARGET_FPS = 0
rott-dev: ATARI_ADAPTIVE_RENDER = 1
rott-dev: ATARI_ADAPTIVE_RENDER_MAX_DIV = 6
rott-dev: ATARI_ADAPTIVE_ACTOR_THROTTLE = 1
rott-dev: ATARI_ACTOR_THROTTLE_DIV = 3
rott-dev: ATARI_ACTOR_THROTTLE_MAX_DIV = 7
rott-dev: ATARI_DYNAMIC_QUALITY = 1
rott-dev: ATARI_DYNAMIC_QUALITY_MAX = 3
rott-dev: ATARI_LOWP = 1
rott-dev: ATARI_RAYCAST_STEP = 8
rott-dev: ATARI_MAX_RAY_STEPS = 20
rott-dev: ATARI_MIN_SPRITE_HEIGHT = 1024
rott-dev: ATARI_ACTOR_BUDGET = 96
rott-dev: ATARI_SPRITE_BUDGET = 96
rott-dev: ATARI_EFFECT_BUDGET = 1
rott-dev: ATARI_FLAT_WORLD = 1
rott-dev: ATARI_VIEW_SCALE_DIV = 2
rott-dev: ATARI_SPRITE_FRAME_DIV = 2
rott-dev: ATARI_HIDE_WEAPON = 1
rott-dev: ATARI_FLAT_WALL_LIGHT = 1
rott-dev: ATARI_SHOW_FPS = 1
rott-dev: rott-huntbgin

# Commercial build
rott-darkwar: ATARI_SHAREWARE = 0
rott-darkwar: ATARI_SUPERROTT = 0
rott-darkwar: ATARI_SITELICENSE = 0
rott-darkwar: atari-stage-runtime-files $(ATARI_OUTPUT_DARKWAR)

# CD build
rott-rottcd: ATARI_SHAREWARE = 0
rott-rottcd: ATARI_SUPERROTT = 0
rott-rottcd: ATARI_SITELICENSE = 0
rott-rottcd: atari-stage-runtime-files $(ATARI_OUTPUT_ROTTCD)

# Site license build
rott-rottsite: ATARI_SHAREWARE = 0
rott-rottsite: ATARI_SUPERROTT = 0
rott-rottsite: ATARI_SITELICENSE = 0
rott-rottsite: atari-stage-runtime-files $(ATARI_OUTPUT_ROTTSITE)

clean:
	$(RM) -r $(ATARI_OUTPUTS) $(OBJDIR)

$(BUILDDIR)/%.TOS: $(ATARI_OBJECTS) | $(BUILDDIR)
	$(ATARI_CC) $(ATARI_LDFLAGS) $(ATARI_OBJECTS) $(ATARI_LIBS) -o $@

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

atari-stage-runtime-files: | $(BUILDDIR)
	@for src in $(ATARI_RUNTIME_CONFIG_FILES); do \
		dst="$(BUILDDIR)/$$(basename "$$src")"; \
		if [ -e "$$src" ]; then \
			cp -f "$$src" "$$dst"; \
		fi; \
	done
	@if [ -d "$(DATADIR)" ]; then \
		for src in $(ATARI_RUNTIME_DATA_FILES); do \
			dst="$(BUILDDIR)/$$(basename "$$src")"; \
			if [ -e "$$src" ] && [ ! -e "$$dst" ]; then \
				cp "$$src" "$$dst"; \
			fi; \
		done; \
	fi

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
