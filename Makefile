EMCC ?= emcc
SRCDIR ?= rott
OUTBASE ?= rott
WEB_PORT ?= 8000
BUILDDIR ?= web/build
OBJDIR ?= web/obj
DATADIR ?= build
SHELLFILE ?= web/emscripten-shell.html

CPPFLAGS := -I. -I$(SRCDIR) \
	-DPLATFORM_UNIX=1 \
	-DSHAREWARE=1 \
	-DSUPERROTT=0 \
	-DSITELICENSE=0 \
	-DC_FIXED_MATH=1

CFLAGS ?= -O2 -std=gnu99 -fno-strict-aliasing
CFLAGS += -Wno-unused-but-set-variable -Wno-unused-variable -Wno-unused-function -Wno-deprecated-declarations
CFLAGS += -Wno-implicit-function-declaration -Wno-int-conversion

SDLFLAGS := -sUSE_SDL=2 -sUSE_SDL_MIXER=2

EMFLAGS := $(SDLFLAGS) \
	-sSDL2_MIXER_FORMATS='["mid","ogg","mp3","wav"]' \
	-sALLOW_MEMORY_GROWTH=1 \
	-sFORCE_FILESYSTEM=1 \
	-sEXIT_RUNTIME=0 \
	-sASYNCIFY \
	-sEMULATE_FUNCTION_POINTER_CASTS=1

PRELOAD := \
	--preload-file $(DATADIR)/HUNTBGIN.WAD@/HUNTBGIN.WAD \
	--preload-file $(DATADIR)/HUNTBGIN.RTL@/HUNTBGIN.RTL \
	--preload-file $(DATADIR)/HUNTBGIN.RTC@/HUNTBGIN.RTC \
	--preload-file $(DATADIR)/REMOTE1.RTS@/REMOTE1.RTS \
	--preload-file $(SRCDIR)/config.rot@/config.rot \
	--preload-file $(SRCDIR)/sound.rot@/sound.rot \
	--preload-file $(SRCDIR)/battle.rot@/battle.rot \
	--preload-file $(SRCDIR)/scores.rot@/scores.rot

SRC := \
	$(SRCDIR)/byteordr.c \
	$(SRCDIR)/cin_actr.c \
	$(SRCDIR)/cin_efct.c \
	$(SRCDIR)/cin_evnt.c \
	$(SRCDIR)/cin_glob.c \
	$(SRCDIR)/cin_main.c \
	$(SRCDIR)/cin_util.c \
	$(SRCDIR)/dosutil.c \
	$(SRCDIR)/engine.c \
	$(SRCDIR)/fx_man.c \
	$(SRCDIR)/i_timer.c \
	$(SRCDIR)/isr.c \
	$(SRCDIR)/modexlib.c \
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

OBJ := $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SRC))

all: $(BUILDDIR)/$(OUTBASE).html

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(EMCC) $(CPPFLAGS) $(CFLAGS) $(SDLFLAGS) -c $< -o $@

$(BUILDDIR)/$(OUTBASE).html: $(OBJ) | $(OBJDIR)
	mkdir -p $(BUILDDIR)
	$(EMCC) $(OBJ) $(EMFLAGS) --shell-file $(SHELLFILE) $(PRELOAD) -o $@

serve: $(BUILDDIR)/$(OUTBASE).html
	@echo "Serving http://localhost:$(WEB_PORT)/$(OUTBASE).html from $(BUILDDIR)"
	@if command -v npx >/dev/null 2>&1; then \
		cd $(BUILDDIR) && npx serve -p $(WEB_PORT); \
	elif command -v emrun >/dev/null 2>&1; then \
		cd $(BUILDDIR) && emrun --no_browser --port $(WEB_PORT) $(OUTBASE).html; \
	elif command -v python3 >/dev/null 2>&1; then \
		cd $(BUILDDIR) && python3 -m http.server $(WEB_PORT); \
	else \
		echo "Error: emrun or python3 is required to run a local web server."; \
		exit 1; \
	fi

clean:
	rm -rf $(OBJDIR)/* $(BUILDDIR)/*

.PHONY: all run-web clean
