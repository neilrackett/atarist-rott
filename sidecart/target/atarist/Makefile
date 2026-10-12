# ROTT Accelerator cartridge stub (m68k), built inside atarist-toolkit-docker.
# Copyright (C) 2026 Neil Rackett
# SPDX-License-Identifier: GPL-3.0-or-later

VERSION := $(shell cat version.txt)

# GEMDOS date and time in the cartridge header: fixed, so that two builds of
# the same source are the same firmware (TOS ignores them). The Mega STE's
# launch, March 1991 (no source gives the day): 1 March 1991, 00:00.
YEAR = 1991
MONTH = 3
DAY = 1
GEMDOS_DATE_FORMATTED := $(shell echo "$$(($(YEAR) - 1980 << 9 | $(MONTH) << 5 | $(DAY)))")
GEMDOS_TIME_FORMATTED := 0

SOURCES_DIR = ./src
BUILD_DIR = ./build
DIST_DIR = ./dist
BIN = BOOT.BIN

VASMFLAGS=-Faout -quiet -x -m68000 -spaces -showopt -devpac -DGEMDOS_DATE=$(GEMDOS_DATE_FORMATTED) -DGEMDOS_TIME=$(GEMDOS_TIME_FORMATTED)
VASM = vasm
VLINK = vlink

.PHONY: all release prepare build dist clean
all: prepare dist
release: prepare dist

prepare: clean
	mkdir -p $(BUILD_DIR)

# Generate the boot banner (banner_txt) with the version substituted from
# version.txt, so main.s can `include version.inc` (as MD/Net and MD/DOOM
# do: vasm will not concatenate -D strings inside a dc.b).
main.o: prepare
	printf 'banner_txt:\tdc.b "ROTT Accelerator $(VERSION)",0\n\teven\n' > $(SOURCES_DIR)/version.inc
	$(VASM) $(VASMFLAGS) $(SOURCES_DIR)/main.s -o $(BUILD_DIR)/main.o

build: main.o
	$(VLINK) $(BUILD_DIR)/main.o -brawbin1 -o $(BUILD_DIR)/$(BIN)

dist: build
	mkdir -p $(DIST_DIR)
	cp $(BUILD_DIR)/$(BIN) $(DIST_DIR)

clean:
	rm -rf $(BUILD_DIR)
	rm -rf $(DIST_DIR)
