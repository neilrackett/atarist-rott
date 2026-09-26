#!/bin/bash
# Run ROTT_ST.TOS unattended in Hatari, with or without the emulated MD/ROTT
# firmware, recording the screen.
# Copyright (C) 2026 Neil Rackett
# SPDX-License-Identifier: GPL-3.0-or-later
#
#   run-hatari.sh <hatari> <tos.img> <st_dir> <out_dir> <vbls> [md|st] [machine] [mhz]
#
# st_dir holds ROTT_ST.TOS (build it with ATARI_MD_AUTOTEST=n so it starts a
# game by itself) and the ROTT data files; it becomes drive C:. With "md",
# <out_dir>/sd/rott/HUNTBGIN.WAD must exist (a copy or a link): that is the
# emulated Multi-device's SD card, and the firmware's own view of every
# frame is written to <out_dir>/md*.ppm. The ST's screen goes to
# <out_dir>/st.avi either way. PROG=PINGTEST.TOS (say) runs another program
# from st_dir instead of ROTT_ST.TOS; MEMSIZE=4096 sets the ST RAM in KiB.
set -euo pipefail

HATARI=$1
TOS=$2
STDIR=$3
OUT=$4
VBLS=$5
MODE=${6:-md}
MACHINE=${7:-megaste}
MHZ=${8:-16}
PROG=${PROG:-ROTT_ST.TOS}
MEM_ARGS=()
[ -n "${MEMSIZE:-}" ] && MEM_ARGS=(--memsize "$MEMSIZE")

mkdir -p "$OUT"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
if [ "$MODE" = "md" ]; then
    export MDROTT_SD="$OUT/sd"
    export MDROTT_DUMP="$OUT"
    export MDROTT_DUMP_EVERY=${MDROTT_DUMP_EVERY:-5}
else
    unset MDROTT_SD MDROTT_DUMP || true
fi

"$HATARI" --machine "$MACHINE" --cpuclock "$MHZ" --tos "$TOS" ${MEM_ARGS[@]+"${MEM_ARGS[@]}"} \
    --harddrive "$STDIR" --auto "C:\\$PROG" \
    --fast-boot on --fast-forward on --sound off --frameskips 0 \
    --avirecord --avi-vcodec png --avi-file "$OUT/st.avi" \
    --run-vbls "$VBLS" --log-file "$OUT/hatari.log" > "$OUT/hatari.out" 2>&1 &
pid=$!
# Watchdog: --run-vbls normally ends it; a hung ST must not hang the caller.
for _ in $(seq 1 ${TIMEOUT_S:-600}); do
    kill -0 $pid 2>/dev/null || break
    sleep 1
done
kill $pid 2>/dev/null || true
wait $pid 2>/dev/null || true
echo "done: $(ls "$OUT" | wc -l) files in $OUT"
