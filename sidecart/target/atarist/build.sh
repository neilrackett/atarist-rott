#!/bin/bash
# Build the MD/ROTT cartridge stub and embed it in the RP firmware as
# rp/src/include/target_firmware.h.
# Copyright (C) 2026 Neil Rackett
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

if [ -z "${1:-}" ] || [ -z "${2:-}" ]; then
    echo "Usage: $0 <working_folder> all|release"
    exit 1
fi

working_folder=$1
build_type=$2
target_firmware="target_firmware.h"

# STCMD_NO_TTY=1: stcmd must not ask docker for a TTY, or the build fails
# silently and a stale BOOT.BIN survives.
STCMD_NO_TTY=1 ST_WORKING_FOLDER="$working_folder" stcmd make "$build_type"

boot_bin="$working_folder/dist/BOOT.BIN"
if [ ! -f "$boot_bin" ]; then
    echo "ERROR: $boot_bin not produced by stcmd make"
    exit 4
fi
if [ "$(uname)" = "Darwin" ]; then
    boot_size=$(stat -f %z "$boot_bin")
else
    boot_size=$(stat -c %s "$boot_bin")
fi
# MD_HEADER_SIZE in rott_md_protocol.h: the status block follows.
header_max=1024
if [ "$boot_size" -gt "$header_max" ]; then
    echo "ERROR: cartridge stub is $boot_size bytes; limit is $header_max"
    exit 5
fi
echo "Cartridge stub: $boot_size / $header_max bytes"

python3 firmware.py --input="$boot_bin" --output="$target_firmware" --array_name=target_firmware
mv "$target_firmware" "../../rp/src/include/$target_firmware"
echo "Wrote rp/src/include/$target_firmware"
