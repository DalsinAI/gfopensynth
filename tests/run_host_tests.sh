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
# the MT-32's memory and its DT1 messages (step 1 of our own LA engine, docs/MT32-ENGINE.md), 64-bit then 32-bit
$CC -O1 -g $SAN -Wall -Wextra -Werror -I"$HERE/src" -o "$OUT/mt32mem_test" "$HERE/tests/mt32mem_test.c" "$HERE/src/gs_mt32mem.c"
"$OUT/mt32mem_test"
if $CC -m32 -o "$OUT/san32" "$OUT/san.c" 2>/dev/null; then
    $CC -m32 -O1 -g -Wall -Wextra -Werror -I"$HERE/src" -o "$OUT/mt32mem_test32" "$HERE/tests/mt32mem_test.c" "$HERE/src/gs_mt32mem.c"
    "$OUT/mt32mem_test32"
fi
# the engine as one translation unit (as the runtime builds it)
$CC -O1 -Wall -Wextra -Werror -I"$HERE/src" -o "$OUT/one_unit" "$HERE/tests/one_unit.c" && "$OUT/one_unit" && echo "PASS: one translation unit"
# GFOpenSynth Basic, our own SoundFont: made, then read and played
$CC -O1 -Wall -Wextra -Werror -o "$OUT/make_basic_sf2" "$HERE/tools/make_basic_sf2.c" -lm
"$OUT/make_basic_sf2" "$OUT/basic.sf2"
$CC -O1 -g $SAN -Wall -Wextra -Werror -I"$HERE/src" -o "$OUT/basic_sf2_test" "$HERE/tests/basic_sf2_test.c" \
    "$HERE/src/gs_midi.c" "$HERE/src/gs_sf2.c" "$HERE/src/gs_synth.c" "$HERE/src/gs_tables.c" -lm
"$OUT/basic_sf2_test" "$OUT/basic.sf2"
# the demo tune through the engine into a WAV (tools/smf2wav.c): it plays to its end
$CC -O1 -Wall -Wextra -Werror -I"$HERE/src" -o "$OUT/smf2wav" "$HERE/tools/smf2wav.c" \
    "$HERE/src/gs_midi.c" "$HERE/src/gs_sf2.c" "$HERE/src/gs_synth.c" "$HERE/src/gs_tables.c" -lm
"$OUT/smf2wav" "$OUT/basic.sf2" "$HERE/demo/gfopensynth-demo.mid" "$OUT/gfopensynth-demo.wav"
# the MIDI collection (midi/, installed to Media:midi): each file has its source and licence, opens and sounds
if command -v python3 >/dev/null 2>&1; then
    python3 "$HERE/tests/midi_collection_check.py" "$HERE/midi"
fi
$CC -O1 -g $SAN -Wall -Wextra -Werror -I"$HERE/src" -o "$OUT/midi_collection_test" "$HERE/tests/midi_collection_test.c" \
    "$HERE/src/gs_midi.c" "$HERE/src/gs_sf2.c" "$HERE/src/gs_synth.c" "$HERE/src/gs_tables.c" -lm
find "$HERE/midi" -name '*.mid' | sort | xargs "$OUT/midi_collection_test" "$OUT/basic.sf2"
