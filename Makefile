SRCDIR ?= rott
DATADIR ?= tmp/ROTT
BUILDDIR ?= build/atari
OBJDIR ?= obj/atari

ATARI_DEBUG ?= 0
ATARI_SHOW_FPS ?= 0
ATARI_TARGET_FPS ?= 12

ATARI_NOIR ?= 0
ATARI_NOIR_DITHERING ?= 0

ATARI_CC ?= m68k-atari-mint-gcc
ATARI_SHAREWARE ?= 1
ATARI_SUPERROTT ?= 0
ATARI_SITELICENSE ?= 0
ATARI_ENABLE_FASTMODE ?= 1
ATARI_ENABLE_KBDINT ?= 1
ATARI_ENABLE_BLITTER ?= 1
ATARI_FADE_SCALE ?= 4
ATARI_C2P_VIEW_ZOOM ?= 1
ATARI_C2P_STRICT_NO_OVERLAP ?= 1
ATARI_C2P_FAST_COPY ?= 1
ATARI_C2P_DIRTY_TILES ?= 1
ATARI_C2P_DIRTY_TILE_THRESHOLD ?= 200
ATARI_MAX_CATCHUP_STEPS ?= 0
ATARI_CATCHUP_POLL_INTERVAL ?= 4
ATARI_RENDER_DIVISOR ?= 2
ATARI_ACTOR_THROTTLE_DIV ?= 3
ATARI_MENU_CURSOR_DELAY_TICS ?= 0
ATARI_MENU_EVENT_DRIVEN ?= 1

ATARI_SKIP_PRECACHE ?= 1
ATARI_SKIP_FADES ?= 1
ATARI_SKIP_LIGHTLEVEL ?= 1
ATARI_SKIP_FIZZLE ?= 1

ATARI_CFLAGS ?= -O2 -fomit-frame-pointer -s -std=gnu99 -m68000 \
	-fno-strict-aliasing -DPLATFORM_TIMER_HZ=200 \
	-DSHAREWARE=$(ATARI_SHAREWARE) -DATARI_ENABLE_FASTMODE=$(ATARI_ENABLE_FASTMODE) \
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
ATARI_RUNTIME_FILES := \
	$(DATADIR)/HUNTBGIN.WAD \
	$(DATADIR)/HUNTBGIN.RTL \
	$(DATADIR)/HUNTBGIN.RTC \
	$(DATADIR)/REMOTE1.RTS \
	$(SRCDIR)/config.rot \
	$(SRCDIR)/sound.rot \
	$(SRCDIR)/battle.rot \
	$(SRCDIR)/scores.rot
ATARI_FLAGS_STAMP := $(OBJDIR)/.atari_flags

all: rott-huntbgin
.PHONY: FORCE rott-huntbgin rott-darkwar rott-rottcd rott-rottsite atari-stage-runtime-files
.SECONDARY: $(ATARI_OBJECTS)
FORCE:

# Shareware build (default)
rott-huntbgin: ATARI_SHAREWARE = 1
rott-huntbgin: ATARI_SUPERROTT = 0
rott-huntbgin: ATARI_SITELICENSE = 0
rott-huntbgin: atari-stage-runtime-files $(ATARI_OUTPUT_SHAREWARE)

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
	@for src in $(ATARI_RUNTIME_FILES); do \
		dst="$(BUILDDIR)/$$(basename "$$src")"; \
		if [ ! -e "$$dst" ]; then \
			cp "$$src" "$$dst"; \
		fi; \
	done

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
