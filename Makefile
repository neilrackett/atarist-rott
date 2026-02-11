CC = m68k-atari-mint-gcc

BUILD_DIR ?= build
OBJ_DIR ?= obj
TARGET ?= ROTT.TOS

SHAREWARE ?= 1
SUPERROTT ?= 0
SITELICENSE ?= 0
ATARI_TARGET_FPS ?= 12
ATARI_TIC_WAIT_MS ?= 0
ATARI_SKIP_FADES ?= 0
ATARI_SKIP_FIZZLE ?= 0
ATARI_SKIP_LIGHTLEVEL ?= 0
ATARI_SHOW_FPS ?= 0

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

INCLUDES := -Irott -Irott/audiolib $(SDL_CFLAGS)

CFLAGS ?= -O2 -m68000 -fomit-frame-pointer -fno-strict-aliasing -ffast-math -std=gnu89
CFLAGS += $(DEFINES) $(INCLUDES)

LDFLAGS ?= -m68000
LDLIBS ?= $(SDL_LIBS) -lm

SOURCES := \
	rott/audio_stubs.c \
	rott/atari_megaste.c \
	rott/byteordr.c \
	rott/cin_actr.c \
	rott/cin_efct.c \
	rott/cin_evnt.c \
	rott/cin_glob.c \
	rott/cin_main.c \
	rott/cin_util.c \
	rott/dosutil.c \
	rott/engine.c \
	rott/i_timer.c \
	rott/isr.c \
	rott/modexlib_sdl.c \
	rott/rt_actor.c \
	rott/rt_battl.c \
	rott/rt_build.c \
	rott/rt_cfg.c \
	rott/rt_com.c \
	rott/rt_crc.c \
	rott/rt_debug.c \
	rott/rt_dmand.c \
	rott/rt_door.c \
	rott/rt_draw.c \
	rott/rt_err.c \
	rott/rt_floor.c \
	rott/rt_game.c \
	rott/rt_in.c \
	rott/rt_main.c \
	rott/rt_map.c \
	rott/rt_menu.c \
	rott/rt_msg.c \
	rott/rt_net.c \
	rott/rt_playr.c \
	rott/rt_rand.c \
	rott/rt_scale.c \
	rott/rt_sound.c \
	rott/rt_spbal.c \
	rott/rt_sqrt.c \
	rott/rt_stat.c \
	rott/rt_state.c \
	rott/rt_str.c \
	rott/rt_swift.c \
	rott/rt_ted.c \
	rott/rt_util.c \
	rott/rt_vid.c \
	rott/rt_view.c \
	rott/scriplib.c \
	rott/w_wad.c \
	rott/watcom.c \
	rott/winrott.c \
	rott/z_zone.c

OBJECTS := $(addprefix $(OBJ_DIR)/,$(SOURCES:.c=.o))
DEPS := $(OBJECTS:.o=.d)

.PHONY: all clean

all: $(BUILD_DIR)/$(TARGET)

$(BUILD_DIR)/$(TARGET): $(OBJECTS) | $(BUILD_DIR)
	$(CC) $(LDFLAGS) $(OBJECTS) $(LDLIBS) -o $@

$(OBJ_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR):
	@mkdir -p $@

clean:
	$(RM) -r $(BUILD_DIR)/$(TARGET) $(OBJECTS) $(DEPS)

-include $(DEPS)
