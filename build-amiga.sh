#!/bin/sh
# gfopensynth.library for AmigaOS 3.x with an os32 GCC 16 stove (bebbo's amiga-gcc,
# GCC 16, libnix), as Cradle's stove builds call it.
#   STOVE  the stove (default ~/AmiCradle/stoves/os32-gcc16); or PREFIX, a
#          toolchain folder with bin/m68k-amigaos-gcc (a GFOpenAmigaGCC
#          release), with NDK_INC naming OpenNDK's Include_H
#   OUT    the output directory (default build-amiga)
#   HOST_TESTS=0  skip the PC tests (tests/run_host_tests.sh)
# Outputs: lib/gfopensynth.library (copy to LIBS:), lib/libgfopensynth.a
# (the link stub: <gfopensynth.h> and -lgfopensynth), c/PlaySynth,
# include/, docs/.
# The FPU check: the library and the stub use no floating point at all (the
# synth is integer only); the build fails if either links a soft-float
# routine.
# MIT, Copyright (c) 2026 AmiCradle Team.
set -eu

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SPECS="$HERE/specs/m68k-soft-m68040.specs"
STOVE=${STOVE:-"$HOME/AmiCradle/stoves/os32-gcc16"}
P=${PREFIX:-"$STOVE/prefix"}
CC="$P/bin/m68k-amigaos-gcc"
AR="$P/bin/m68k-amigaos-ar"
NM="$P/bin/m68k-amigaos-nm"
OUT=${OUT:-"$HERE/build-amiga"}
INC="-I$HERE/include -I$HERE/src"
[ -n "${NDK_INC:-}" ] && INC="$INC -I$NDK_INC"

if command -v python3 >/dev/null 2>&1; then
    python3 "$HERE/tools/gen_headers.py" --check
    python3 "$HERE/tools/gen_tables.py" --check
fi
if [ "${HOST_TESTS:-1}" != 0 ] && command -v "${HOST_CC:-cc}" >/dev/null 2>&1; then
    OUT="$OUT/host" sh "$HERE/tests/run_host_tests.sh"
fi

mkdir -p "$OUT/lib" "$OUT/obj" "$OUT/docs" "$OUT/include" "$OUT/c"
cp -R "$HERE/include/." "$OUT/include/"
cp "$HERE/docs/gfopensynth.md" "$OUT/docs/"

# no loops turned into memset/memcpy calls, and address 0 is memory on an Amiga
CFLAGS="-m68040 -specs=$SPECS -Os -fno-tree-loop-distribute-patterns -fno-delete-null-pointer-checks -fomit-frame-pointer -Wall -Wextra -Werror $INC"

"$CC" $CFLAGS -mcrt=nix20 -fno-toplevel-reorder -nostartfiles -nostdlib \
  -o "$OUT/lib/gfopensynth.library" "$HERE/library/gs_lib.c" "$HERE/library/gs_player.c" \
  "$HERE/src/gs_synth.c" "$HERE/src/gs_sf2.c" "$HERE/src/gs_midi.c" "$HERE/src/gs_tables.c" -lgcc -Wl,-Map,"$OUT/lib/gfopensynth.library.map"
echo "$OUT/lib/gfopensynth.library ($(wc -c < "$OUT/lib/gfopensynth.library") bytes)"

"$CC" $CFLAGS -noixemul -c "$HERE/stub/gfopensynth_stub.c" -o "$OUT/obj/gfopensynth_stub.o"
rm -f "$OUT/lib/libgfopensynth.a"
"$AR" rcs "$OUT/lib/libgfopensynth.a" "$OUT/obj/gfopensynth_stub.o"

# the FPU check
FLOAT='__(add|sub|mul|div|neg|cmp|eq|ne|lt|le|gt|ge|unord)(s|d|x)f[23]|__(fix|fixuns|float|floatun)(s|d|x)(i|d)f|__extend|__trunc|IEEEDP|IEEESP|SPFix|SPFlt'
for f in "$OUT/lib/gfopensynth.library" "$OUT/obj/gfopensynth_stub.o"; do
    if "$NM" "$f" 2>/dev/null | grep -E "$FLOAT" >/dev/null; then
        echo "FPU check: $f uses floating point:"; "$NM" "$f" | grep -E "$FLOAT"; exit 1
    fi
done
echo "FPU check: no floating point in gfopensynth.library or libgfopensynth.a"
# the library must need nothing from outside (no C library)
if "$NM" -u "$OUT/lib/gfopensynth.library" | grep . ; then
    echo "gfopensynth.library has undefined symbols"; exit 1
fi

"$CC" $CFLAGS -noixemul -o "$OUT/c/PlaySynth" "$HERE/command/playsynth.c" -L"$OUT/lib" -lgfopensynth
echo "$OUT/c/PlaySynth ($(wc -c < "$OUT/c/PlaySynth") bytes)"
(cd "$OUT" && sha256sum lib/gfopensynth.library lib/libgfopensynth.a c/PlaySynth > SHA256SUMS)
cat "$OUT/SHA256SUMS"
