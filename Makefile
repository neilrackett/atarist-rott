CC = m68k-atari-mint-gcc

SRCDIR ?= rott
DATADIR ?= tmp/ROTT
BUILDDIR ?= build/atari
OBJDIR ?= obj/atari
TARGET ?= ROTT_TT.TOS

SHAREWARE ?= 1
SUPERROTT ?= 0
SITELICENSE ?= 0
ATARI_TARGET_FPS ?= 12
ATARI_TIC_WAIT_MS ?= 0
ATARI_SKIP_FADES ?= 0
ATARI_SKIP_FIZZLE ?= 0
ATARI_SKIP_LIGHTLEVEL ?= 0
ATARI_SHOW_FPS ?= 0

ATARI_RUNTIME_FILES := \
	$(DATADIR)/HUNTBGIN.WAD \
	$(DATADIR)/HUNTBGIN.RTL \
	$(DATADIR)/HUNTBGIN.RTC \
	$(DATADIR)/REMOTE1.RTS \
	$(SRCDIR)/config.rot \
	$(SRCDIR)/sound.rot \
	$(SRCDIR)/battle.rot \
	$(SRCDIR)/scores.rot

SDL_CFLAGS ?= $(shell m68k-atari-mint-pkg-config --cflags sdl 2>/dev/null)
SDL_LIBS ?= $(shell m68k-atari-mint-pkg-config --libs sdl 2>/dev/null)

ifeq ($(strip $(SDL_CFLAGS)),)
SDL_CFLAGS := -I/usr/m68k-atari-mint/include/SDL -D_GNU_SOURCE=1
endif
ifeq ($(strip $(SDL_LIBS)),)
SDL_LIBS := -lSDL -lgem -lldg -lgem
endif

DEFINES := \
	-DPLATFORM_UNIX=1 \
	-DSHAREWARE=$(SHAREWARE) \
	-DSUPERROTT=$(SUPERROTT) \
	-DSITELICENSE=$(SITELICENSE) \
	-DATARI_TARGET_FPS=$(ATARI_TARGET_FPS) \
	-DATARI_TIC_WAIT_MS=$(ATARI_TIC_WAIT_MS) \
	-DATARI_SKIP_FADES=$(ATARI_SKIP_FADES) \
	-DATARI_SKIP_FIZZLE=$(ATARI_SKIP_FIZZLE) \
	-DATARI_SKIP_LIGHTLEVEL=$(ATARI_SKIP_LIGHTLEVEL) \
	-DATARI_SHOW_FPS=$(ATARI_SHOW_FPS) \
	-DC_FIXED_MATH=1 \
	-DUSE_SDL=0

INCLUDES := -I$(SRCDIR) -I$(SRCDIR)/audiolib $(SDL_CFLAGS)

CFLAGS ?= -O2 -m68000 -fomit-frame-pointer -fno-strict-aliasing -ffast-math -std=gnu89
CFLAGS += $(DEFINES) $(INCLUDES)

LDFLAGS ?= -m68000
LDLIBS ?= $(SDL_LIBS) -lm

SOURCES := \
	$(SRCDIR)/audio_stubs.c \
	$(SRCDIR)/atari_megaste.c \
	$(SRCDIR)/byteordr.c \
	$(SRCDIR)/cin_actr.c \
	$(SRCDIR)/cin_efct.c \
	$(SRCDIR)/cin_evnt.c \
	$(SRCDIR)/cin_glob.c \
	$(SRCDIR)/cin_main.c \
	$(SRCDIR)/cin_util.c \
	$(SRCDIR)/dosutil.c \
	$(SRCDIR)/engine.c \
	$(SRCDIR)/i_timer.c \
	$(SRCDIR)/isr.c \
	$(SRCDIR)/modexlib_sdl.c \
	$(SRCDIR)/rt_actor.c \
	$(SRCDIR)/rt_battl.c \
	$(SRCDIR)/rt_build.c \
	$(SRCDIR)/rt_cfg.c \
	$(SRCDIR)/rt_com.c \
	$(SRCDIR)/rt_crc.c \
	$(SRCDIR)/rt_debug.c \
	$(SRCDIR)/rt_dmand.c \
	$(SRCDIR)/rt_door.c \
	$(SRCDIR)/rt_draw.c \
	$(SRCDIR)/rt_err.c \
	$(SRCDIR)/rt_floor.c \
	$(SRCDIR)/rt_game.c \
	$(SRCDIR)/rt_in.c \
	$(SRCDIR)/rt_main.c \
	$(SRCDIR)/rt_map.c \
	$(SRCDIR)/rt_menu.c \
	$(SRCDIR)/rt_msg.c \
	$(SRCDIR)/rt_net.c \
	$(SRCDIR)/rt_playr.c \
	$(SRCDIR)/rt_rand.c \
	$(SRCDIR)/rt_scale.c \
	$(SRCDIR)/rt_sound.c \
	$(SRCDIR)/rt_spbal.c \
	$(SRCDIR)/rt_sqrt.c \
	$(SRCDIR)/rt_stat.c \
	$(SRCDIR)/rt_state.c \
	$(SRCDIR)/rt_str.c \
	$(SRCDIR)/rt_swift.c \
	$(SRCDIR)/rt_ted.c \
	$(SRCDIR)/rt_util.c \
	$(SRCDIR)/rt_vid.c \
	$(SRCDIR)/rt_view.c \
	$(SRCDIR)/scriplib.c \
	$(SRCDIR)/w_wad.c \
	$(SRCDIR)/watcom.c \
	$(SRCDIR)/winrott.c \
	$(SRCDIR)/z_zone.c

OBJECTS := $(addprefix $(OBJDIR)/,$(SOURCES:.c=.o))
DEPS := $(OBJECTS:.o=.d)

.PHONY: all clean atari-stage-runtime-files

all: $(BUILDDIR)/$(TARGET)

$(BUILDDIR)/$(TARGET): atari-stage-runtime-files $(OBJECTS) | $(BUILDDIR)
	$(CC) $(LDFLAGS) $(OBJECTS) $(LDLIBS) -o $@

$(OBJDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILDDIR):
	@mkdir -p $@

atari-stage-runtime-files: | $(BUILDDIR)
	@if [ -d "$(DATADIR)" ]; then \
		for src in $(ATARI_RUNTIME_FILES); do \
			dst="$(BUILDDIR)/$$(basename "$$src")"; \
			if [ -e "$$src" ] && [ ! -e "$$dst" ]; then \
				cp "$$src" "$$dst"; \
			fi; \
		done; \
	fi

clean:
	$(RM) -r $(BUILDDIR)/$(TARGET) $(OBJECTS) $(DEPS)

-include $(DEPS)
