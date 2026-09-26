# MD/ROTT firmware emulator library (host). See mdemu.h.
# Copyright (C) 2026 Neil Rackett
# SPDX-License-Identifier: GPL-3.0-or-later

RP := ../../rp/src
ROTT := ../../../rott
CC ?= cc
CFLAGS ?= -O2 -g -Wall -Wno-unused-function -Wno-missing-braces
CPPFLAGS := -Ishim -I. -I$(RP)/include -I$(RP) -I../../include -I$(ROTT) \
	-DRELEASE_VERSION=\"emu\" -D_DEBUG=1
OUT ?= build

SRCS := mdemu.c $(RP)/md_main.c $(RP)/md_proto.c $(RP)/tprotocol.c \
	$(RP)/md_pack.c $(RP)/md_video.c $(wildcard $(RP)/rott/*.c) \
	$(ROTT)/atari_tables.c
OBJS := $(patsubst %.c,$(OUT)/%.o,$(notdir $(SRCS)))

vpath %.c . $(RP) $(RP)/rott $(ROTT)

all: $(OUT)/libmdemu.a $(OUT)/selftest $(OUT)/packtest

$(OUT)/%.o: %.c | $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/libmdemu.a: $(OBJS)
	rm -f $@
	ar rcs $@ $^

# Firmware round trip without Hatari: HELLO, level, frames, PPMs.
#   build/selftest <sd dir holding rott/HUNTBGIN.WAD> <output dir>
$(OUT)/selftest: selftest.c $(OUT)/libmdemu.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(OUT)/libmdemu.a -o $@

# The level pack's demand-loading ring under random load.
#   build/packtest <sd dir holding rott/HUNTBGIN.WAD>
$(OUT)/packtest: packtest.c $(OUT)/libmdemu.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(OUT)/libmdemu.a -o $@

-include $(OBJS:.o=.d)

$(OUT):
	mkdir -p $(OUT)

clean:
	rm -rf $(OUT)

.PHONY: all clean
