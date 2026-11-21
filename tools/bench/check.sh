#!/bin/sh
# tools/bench/check.sh NAME MACHINE BUILDDIR
#
# Runs an ATARI_LOGIC_CHECK=4 build of the game (no Accelerator) in
# EmuMD's Hatari to the end of its script and leaves LOGIC.TXT, RENDER.TXT
# and the RENDnnn.RAW frames in OUT/NAME.d/drive: compare two builds'
# with cmp and tools/bench/rawdiff.py. MACHINE: tt, falcon or mste.
set -e
name=$1; mach=$2; build=$3
R=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${OUT:-$R/tmp/bench}
TOSDIR=${TOSDIR:-$R/../atarist-reminiscence/tmp/tos}
HATARI=${HATARI:-$HOME/.cache/emumd/hatari-fork/build/src/hatari}
case $mach in
tt) m=tt; tos="$TOSDIR/TOS 3.06 - UK.rom"; args="--cpuclock 32"; vbls=${VBLS:-14000} ;;
falcon) m=falcon; tos="$TOSDIR/TOS 4.04 - UK.img"; args="--cpuclock 16 --monitor vga"; vbls=${VBLS:-24000} ;;
mste) m=megaste; tos="$TOSDIR/TOS 2.06 - UK - Original.IMG"; args="--cpuclock 16"; vbls=${VBLS:-40000} ;;
*) echo "machine?"; exit 1 ;;
esac
d=$OUT/$name.d
mkdir -p "$d/drive" "$d/sd" "$d/home"
cp -R "$build"/. "$d/drive"/
rm -f "$d/drive/LOGIC.TXT" "$d/drive/RENDER.TXT"
HOME="$d/home" SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy "$HATARI" --confirm-quit false \
    --tos "$tos" --machine $m --harddrive "$d/drive" --md-sd "$d/sd" \
    --sound off --fast-boot on --fast-forward on --run-vbls $vbls \
    --memsize 4 $args --auto 'C:\ROTT_ST.TOS' > "$OUT/$name.log" 2>&1 || true
echo "$name: $(wc -l < "$d/drive/LOGIC.TXT") logic lines, $(wc -l < "$d/drive/RENDER.TXT") render lines"
