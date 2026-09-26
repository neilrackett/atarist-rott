# MD/ROTT: Multi-device microfirmware and test tools.
# Copyright (C) 2026 Neil Rackett
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Run from the host (not inside stcmd): the firmware needs
# PICO_TOOLCHAIN_PATH; the m68k parts call stcmd themselves.
#
#   make build      release firmware (version.txt as-is)
#   make debug      debug firmware (UART console) + test apps
#   make tests      the ST test apps in tests/
#   make emu        firmware emulator library + self-tests (host, needs WAD)
#   make hatari     Hatari with the emulated cartridge (tests/emu)
#   make census     sizes in the shareware WAD vs the pack window
#   make uart       serial console

VERSION := $(shell cat version.txt)
ROOT_DIR := $(abspath ..)
DEFAULT_APP_UUID_KEY := 44444444-4444-4444-8444-444444444444
UUID_FROM_FILE := $(strip $(shell [ -f uuid.txt ] && tr -d '\r\n' < uuid.txt))
APP_UUID_KEY_RESOLVED := $(if $(strip $(APP_UUID_KEY)),$(strip $(APP_UUID_KEY)),$(if $(UUID_FROM_FILE),$(UUID_FROM_FILE),$(DEFAULT_APP_UUID_KEY)))
BOARD ?= pico_w
UART_BAUD ?= 115200
UART_DEV ?=
WAD ?= $(ROOT_DIR)/tmp/ROTT/HUNTBGIN.WAD

export STCMD_NO_TTY = 1

ST_CC = ST_WORKING_FOLDER=$(ROOT_DIR) stcmd m68k-atari-mint-gcc
ST_TEST_FLAGS = -O2 -std=gnu99 -m68000 -I sidecart/include -I rott
ST_TEST_LIBS = rott/sidecart_md.c rott/sidecart_stubs.S

.PHONY: build
build:
	@echo "Using APP_UUID_KEY: $(APP_UUID_KEY_RESOLVED)"
	SKIP_VERSION_BUMP=1 ./build.sh $(BOARD) release "$(APP_UUID_KEY_RESOLVED)"

.PHONY: debug
debug: tests
	@echo "Using APP_UUID_KEY: $(APP_UUID_KEY_RESOLVED)"
	./build.sh $(BOARD) debug "$(APP_UUID_KEY_RESOLVED)"

## Standalone ST test apps (M1): detection + transport, upload/copy timing.
.PHONY: tests pingtest uptest
tests: pingtest uptest

pingtest:
	$(ST_CC) $(ST_TEST_FLAGS) -o sidecart/tests/PINGTEST.TOS \
		sidecart/tests/pingtest.c $(ST_TEST_LIBS)

uptest:
	$(ST_CC) $(ST_TEST_FLAGS) -o sidecart/tests/UPTEST.TOS \
		sidecart/tests/uptest.c rott/atari_md_s.S $(ST_TEST_LIBS)

## The firmware on the host (tests/emu): libmdemu, its self-test, and a
## Hatari with the emulated cartridge. See tests/emu/run-hatari.sh.
.PHONY: emu
emu:
	$(MAKE) -C tests/emu
	mkdir -p tests/emu/build/sd/rott tests/emu/build/selftest-out
	ln -sf $(WAD) tests/emu/build/sd/rott/$(notdir $(WAD))
	tests/emu/build/selftest tests/emu/build/sd tests/emu/build/selftest-out
	tests/emu/build/packtest tests/emu/build/sd

.PHONY: hatari
hatari:
	tests/emu/build-hatari.sh

.PHONY: census
census:
	python3 tools/rottpack.py census $(WAD)

## List candidate UART devices (macOS + Linux)
.PHONY: uart-list
uart-list:
	@for p in /dev/cu.usbmodem* /dev/cu.usbserial* /dev/tty.usbmodem* /dev/tty.usbserial* /dev/ttyACM* /dev/ttyUSB*; do \
		[ -e "$$p" ] && echo "$$p"; \
	done; true

## Open a UART console on the first matching serial device.
## Override with: make uart UART_DEV=/dev/cu.usbmodemXXXX
.PHONY: uart
uart:
	@dev="$(UART_DEV)"; \
	if [ -z "$$dev" ]; then \
		dev="$$(for p in /dev/cu.usbmodem* /dev/cu.usbserial* /dev/tty.usbmodem* /dev/tty.usbserial* /dev/ttyACM* /dev/ttyUSB*; do [ -e "$$p" ] && echo "$$p"; done | head -n 1)"; \
	fi; \
	if [ -z "$$dev" ]; then \
		echo "No serial device found. Run: make uart-list"; \
		exit 1; \
	fi; \
	echo "Opening UART on $$dev at $(UART_BAUD) baud"; \
	echo "Detach with Ctrl-A then K"; \
	screen "$$dev" $(UART_BAUD)
