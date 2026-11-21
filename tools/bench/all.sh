#!/bin/sh
# tools/bench/all.sh TAG [REF] [machines...]
#
# From the host: builds the check and benchmark games from the working
# tree, checks the pictures and the game logic against REF's run (default
# chk0; "-" for none), then times the renderer on each machine (default tt
# and st8). Lines to keep are appended to tmp/bench/results.txt.
tag=$1; ref=${2:-chk0}; shift; shift
[ $# -gt 0 ] || set -- tt ttr st8
R=$(cd "$(dirname "$0")/../.." && pwd)
cd "$R" || exit 1
OUT=$R/tmp/bench
mkdir -p "$OUT"
if ! make st ATARI_LOGIC_CHECK=4 BUILDDIR=build/chk OBJDIR=obj/chk > "$OUT/$tag.build.log" 2>&1 ||
   ! STCMD_NO_TTY=1 STCMD_QUIET=1 stcmd make -j4 -f tools/bench/prof.mk st ATARI_MD_AUTOTEST=8 \
       BUILDDIR=build/bench OBJDIR=obj/bench >> "$OUT/$tag.build.log" 2>&1; then
    grep -E "error|Error |undefined reference" "$OUT/$tag.build.log" | head -20
    echo "build failed"
    exit 1
fi
tools/bench/check.sh ${tag}_chk tt build/chk &
[ -z "$CHECK_MSTE" ] || VBLS=60000 tools/bench/check.sh ${tag}_chkms mste build/chk &
for m in "$@"; do
    tools/bench/bench.sh ${tag}_$m $m build/bench > "$OUT/${tag}_$m.txt" 2>&1 &
done
wait
if [ "$ref" != "-" ]; then
    cmp "$OUT/${tag}_chk.d/drive/LOGIC.TXT" "$OUT/${ref}_chk.d/drive/LOGIC.TXT" \
        && echo "logic: same as $ref"
    refd="$OUT/${ref}_chk.d/drive"
    python3 tools/bench/rawdiff.py "$refd" "$OUT/${tag}_chk.d/drive"
    [ -z "$CHECK_MSTE" ] || python3 tools/bench/rawdiff.py "$OUT/${ref}_chkms.d/drive" "$OUT/${tag}_chkms.d/drive"
fi
for m in "$@"; do
    head -3 "$OUT/${tag}_$m.txt"
    echo "$tag $(head -1 "$OUT/${tag}_$m.txt")" >> "$OUT/results.txt"
done
