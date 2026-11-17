#!/bin/sh
# gfopensynth PC tests: the engine (MIDI parser, SMF player, ROM finder,
# SoundFont reader, synth), with AddressSanitizer and UBSan when the
# compiler has them, then again as 32-bit code (long is 32 bits, as on the
# 68k). No real ROM or SoundFont is used: the test writes its own font.
# MIT, Copyright (c) 2026 AmiCradle Team.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT=${OUT:-"$HERE/build-host"}
CC=${HOST_CC:-cc}
SRC="$HERE/src/gs_midi.c $HERE/src/gs_rom.c $HERE/src/gs_sf2.c $HERE/src/gs_synth.c $HERE/src/gs_tables.c"
mkdir -p "$OUT"
if command -v python3 >/dev/null 2>&1; then
    python3 "$HERE/tools/gen_tables.py" --check
fi
SAN="-fsanitize=address,undefined -fno-sanitize-recover=all"
echo 'int main(void){return 0;}' > "$OUT/san.c"
$CC $SAN -o "$OUT/san" "$OUT/san.c" 2>/dev/null || SAN=
$CC -O1 -g $SAN -Wall -Wextra -Werror -I"$HERE/src" -o "$OUT/synth_test" "$HERE/tests/synth_test.c" $SRC -lm
"$OUT/synth_test"
if $CC -m32 -o "$OUT/san32" "$OUT/san.c" 2>/dev/null; then
    $CC -m32 -O1 -g -Wall -Wextra -Werror -I"$HERE/src" -o "$OUT/synth_test32" "$HERE/tests/synth_test.c" $SRC -lm
    "$OUT/synth_test32"
fi
