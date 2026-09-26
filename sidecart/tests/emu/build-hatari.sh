#!/bin/bash
# Build Hatari with the emulated MD/ROTT cartridge: libmdemu (this
# directory) linked in through hatari-mdrott.patch.
# Copyright (C) 2026 Neil Rackett
# SPDX-License-Identifier: GPL-3.0-or-later
#
#   build-hatari.sh [hatari source dir]
#
# Without an argument Hatari v2.6.1 is cloned into build/hatari-src. The
# patch is applied once; later runs just rebuild (after libmdemu changes,
# Hatari has to be relinked). Prints the path of the hatari binary.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
SRC=${1:-$HERE/build/hatari-src}
HATARI_GIT=${HATARI_GIT:-https://framagit.org/hatari/hatari.git}
HATARI_TAG=${HATARI_TAG:-v2.6.1}

make -C "$HERE" >&2

if [ ! -d "$SRC" ]; then
    git clone --depth 1 --branch "$HATARI_TAG" "$HATARI_GIT" "$SRC" >&2
fi
if ! grep -q MDROTT_EMU "$SRC/src/cpu/memory.c"; then
    patch -d "$SRC" -p1 < "$HERE/hatari-mdrott.patch" >&2
fi
if ! grep -q MDROTT_EMU_INCLUDE "$SRC/src/cpu/CMakeLists.txt"; then
    cat >> "$SRC/src/cpu/CMakeLists.txt" <<'CMAKE'

# MD/ROTT: the Multi-device firmware emulator's API (atarist-rott)
if(MDROTT_EMU_INCLUDE)
	target_compile_definitions(UaeCpu PRIVATE MDROTT_EMU)
	target_include_directories(UaeCpu PRIVATE ${MDROTT_EMU_INCLUDE})
endif()
CMAKE
fi
if ! grep -q MDROTT_EMU_LIB "$SRC/src/CMakeLists.txt"; then
    cat >> "$SRC/src/CMakeLists.txt" <<'CMAKE'

# MD/ROTT: link the Multi-device firmware emulator (atarist-rott)
if(MDROTT_EMU_LIB)
	target_link_libraries(${APP_NAME} ${MDROTT_EMU_LIB})
endif()
CMAKE
fi

ARCH_ARGS=()
if [ "$(uname -s)" = "Darwin" ]; then
    # libmdemu is built for the host; make Hatari match it.
    ARCH_ARGS=(-DCMAKE_OSX_ARCHITECTURES="$(uname -m)")
fi
cmake -S "$SRC" -B "$SRC/build" -DCMAKE_BUILD_TYPE=Release \
    -DENABLE_OSX_BUNDLE=0 \
    -DMDROTT_EMU_INCLUDE="$HERE" \
    -DMDROTT_EMU_LIB="$HERE/build/libmdemu.a" \
    ${ARCH_ARGS[@]+"${ARCH_ARGS[@]}"} >&2
cmake --build "$SRC/build" -j8 >&2
echo "$SRC/build/src/hatari"
