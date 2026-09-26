#!/bin/bash
# Build the MD/ROTT RP2040 firmware. From md-doom's rp/build.sh.
# Copyright (C) 2026 Neil Rackett
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

cd "$(dirname "$0")/.."   # sidecart/
SIDECART_DIR=$PWD

# Stale submodule index locks from interrupted runs block every update.
GIT_DIR=$(git rev-parse --git-dir)
find "$GIT_DIR" -name "index.lock" -path "*modules*" -delete 2>/dev/null || true

# Fetch the SDKs if this is a fresh clone, then pin the versions.
for sm in pico-sdk pico-extras fatfs-sdk; do
    if [ ! -e "$sm/.git" ]; then
        git submodule update --init --recursive "$sm"
    fi
done
(cd pico-sdk && git checkout -q tags/2.2.0 && git submodule update --init -q)
(cd pico-extras && git checkout -q tags/sdk-2.2.0)
(cd fatfs-sdk && git checkout -q 6bdb39f96fe8b897aff12bf3416e32515792e318)

export PICO_SDK_PATH=$SIDECART_DIR/pico-sdk
export FATFS_SDK_PATH=$SIDECART_DIR/fatfs-sdk
export PICO_EXTRAS_PATH=$SIDECART_DIR/pico-extras

cd rp

export RELEASE_VERSION=$(tr -d '\r\n ' < version.txt)
export RELEASE_DATE=$(date +"%Y-%m-%d %H:%M:%S")
export BOARD_TYPE=${1:-pico_w}
export PICO_BOARD=$BOARD_TYPE
BUILD_TYPE=$(echo "${2:-release}" | tr '[:upper:]' '[:lower:]')
echo "Release version: $RELEASE_VERSION, board $BOARD_TYPE, $BUILD_TYPE"

if [ "$BUILD_TYPE" = "release" ]; then
    export DEBUG_MODE=0
    PRESET_KIND="release"
else
    export DEBUG_MODE=1
    PRESET_KIND="debug"
fi

PRESET="${BOARD_TYPE}-${PRESET_KIND}"
BUILD_DIR="build-${BOARD_TYPE}-${PRESET_KIND}"
rm -rf "$BUILD_DIR"
(
    cd src
    cmake --preset "$PRESET"
    cmake --build --preset "$PRESET"
)

mkdir -p dist
if [ "$BUILD_TYPE" = "release" ]; then
    cp "$BUILD_DIR/rp.uf2" "dist/rp-$BOARD_TYPE.uf2"
else
    cp "$BUILD_DIR/rp.uf2" "dist/rp-$BOARD_TYPE-$BUILD_TYPE.uf2"
fi
