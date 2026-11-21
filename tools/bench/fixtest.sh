#!/bin/sh
# tools/bench/fixtest.sh: builds tools/bench/fixtest.c with rott/atari_cpu.S
# and runs it on Hatari's TT (both paths) and an 8MHz STE (the 68000's);
# prints each FIXTEST.TXT.
set -e
R=$(cd "$(dirname "$0")/../.." && pwd)
cd "$R"
OUT=${OUT:-$R/tmp/bench}
TOSDIR=${TOSDIR:-$R/../atarist-reminiscence/tmp/tos}
HATARI=${HATARI:-$HOME/.cache/emumd/hatari-fork/build/src/hatari}
mkdir -p build/fixtest
STCMD_NO_TTY=1 STCMD_QUIET=1 stcmd m68k-atari-mint-gcc -O2 -m68000 \
    -o build/fixtest/FIXTEST.TOS tools/bench/fixtest.c rott/atari_cpu.S \
    -Wl,--wrap=__mulsi3 -Wl,--wrap=__udivsi3 -Wl,--wrap=__divsi3 -Wl,--wrap=__umodsi3 \
    -Wl,--wrap=__modsi3 -Wl,--wrap=__muldi3 -Wl,--wrap=__divdi3
for m in tt ste; do
    d=$OUT/fixtest_$m.d
    rm -rf "$d"; mkdir -p "$d/drive" "$d/home"
    cp build/fixtest/FIXTEST.TOS "$d/drive/"
    case $m in
    tt) a="--machine tt --cpuclock 32"; tos="$TOSDIR/TOS 3.06 - UK.rom" ;;
    ste) a="--machine ste --cpuclock 8"; tos="$TOSDIR/TOS 1.62 - UK.img" ;;
    esac
    HOME="$d/home" SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy "$HATARI" --confirm-quit false \
        --tos "$tos" $a --harddrive "$d/drive" --sound off --fast-boot on --fast-forward on \
        --run-vbls ${VBLS:-30000} --memsize 4 --auto 'C:\FIXTEST.TOS' > "$OUT/fixtest_$m.log" 2>&1 || true
    echo "$m:"; cat "$d/drive/FIXTEST.TXT" 2>/dev/null || echo "  no FIXTEST.TXT"
done
