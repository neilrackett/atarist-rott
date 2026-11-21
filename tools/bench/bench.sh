#!/bin/sh
# tools/bench/bench.sh NAME MACHINE BUILDDIR [first last]
#
# Times the native renderer (no Accelerator) in EmuMD's Hatari with its
# profiler: an ATARI_MD_AUTOTEST=8 build linked with symbols (tools/bench/
# prof.mk) turns 1/64 of a circle a frame, so frames `first` to `last` (70
# to 134 unless given: one whole turn at the start of E1L1, after the
# autotest's screenshots) are the same
# pictures in every build. Prints the time a frame and where it went
# (tools/bench/agg.py), from OUT/NAME.log.
#
# MACHINE: tt (32MHz 68030, 256 colours, 4MB of ST RAM), ttr (the same
# with 2MB of ST RAM and 4MB of TT RAM, as Neil's TT), falcon, st8 (an STE at 8MHz),
# mste (a Mega STE at 16MHz). TOS images from TOSDIR.
set -e
name=$1; mach=$2; build=$3; first=${4:-70}; last=${5:-134}
R=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${OUT:-$R/tmp/bench}
TOSDIR=${TOSDIR:-$R/../atarist-reminiscence/tmp/tos}
HATARI=${HATARI:-$HOME/.cache/emumd/hatari-fork/build/src/hatari}
case $mach in
tt) m=tt; tos="$TOSDIR/TOS 3.06 - UK.rom"; args="--cpuclock 32 --data-cache on"; vbls=9000 ;;
ttr) m=tt; tos="$TOSDIR/TOS 3.06 - UK.rom"; args="--cpuclock 32 --data-cache on --memsize 2 --ttram 4 --addr24 off"; vbls=9000 ;;
falcon) m=falcon; tos="$TOSDIR/TOS 4.04 - UK.img"; args="--cpuclock 16 --monitor vga"; vbls=12000 ;;
mste) m=megaste; tos="$TOSDIR/TOS 2.06 - UK - Original.IMG"; args="--cpuclock 16"; vbls=20000 ;;
st8) m=ste; tos="$TOSDIR/TOS 1.62 - UK.img"; args="--cpuclock 8"; vbls=40000 ;;
*) echo "machine?"; exit 1 ;;
esac
d=$OUT/$name.d
mkdir -p "$d/drive" "$d/sd" "$d/home"
cp -R "$build"/. "$d/drive"/
cat > "$d/start.ini" <<INI
b VBL = 500 :once :trace :file $d/b0.ini
INI
cat > "$d/b0.ini" <<INI
symbols prg
b ( _atari_autotest_frame ).l = $first :once :trace :file $d/b1.ini
INI
cat > "$d/b1.ini" <<INI
profile on
b ( _atari_autotest_frame ).l = $last :once :trace :file $d/b2.ini
INI
cat > "$d/b2.ini" <<INI
profile stats
profile cycles 20000
profile symbols 3000
profile callers
quit
INI
HOME="$d/home" SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy "$HATARI" --confirm-quit false \
    --tos "$tos" --machine $m --harddrive "$d/drive" --md-sd "$d/sd" \
    --sound off --fast-boot on --fast-forward on --run-vbls $vbls \
    --memsize 4 --cpu-exact on $args --auto 'C:\ROTT_ST.TOS' \
    --parse "$d/start.ini" > "$OUT/$name.log" 2>&1 || true
python3 "$R/tools/bench/agg.py" "$OUT/$name.log" "$build/ROTT_ST.nm" $((last - first))
