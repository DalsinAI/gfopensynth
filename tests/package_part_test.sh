#!/bin/sh
# package.sh's part: the MIDI collection goes to MediaFiles: (the standard build's DH4:), never to Media:, whose
# name is the MEDIA: handler's (Dale, 19 Nov 2026: "change dh4 to MediaFiles"). Runs package.sh on stand-in files.
#   sh tests/package_part_test.sh
# MIT, Copyright (c) 2026 AmiCradle Team.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
for f in lib/gfopensynth.library lib/libgfopensynth.a c/PlaySynth soundfonts/basic.sf2 demo/gfopensynth-demo.mid; do
    mkdir -p "$T/out/$(dirname "$f")"; echo stand-in > "$T/out/$f"
done
mkdir -p "$T/out/include"
sh "$HERE/package.sh" "$T/out" "$T/dest" 0.0-test >/dev/null
tar -xzf "$T/dest/gfopensynth-0.0-test-part.tar.gz" -C "$T"
part="$T/Parts/GFOpenSynth/Part"
fail=0
n=$(find "$HERE/midi" -type f | wc -l)
got=$(grep -c '^when MediaDrive add "Media/midi/[^"]*" "MediaFiles:midi/[^"]*"$' "$part" || true)
if [ "$got" -eq "$n" ]; then echo "ok   every one of the $n MIDI collection files goes to MediaFiles:midi"
else echo "FAIL $got of $n MIDI collection lines go to MediaFiles:midi"; fail=1; fi
if grep -q '"Media:' "$part"; then echo "FAIL a line still names Media:"; grep '"Media:' "$part" | head -3; fail=1
else echo "ok   no line names Media:"; fi
grep -q '"MediaFiles:midi/Dance/copper-list.mid"$' "$part" && echo "ok   GFOpenBlanker's tune is at MediaFiles:midi/Dance/copper-list.mid" ||
    { echo "FAIL no MediaFiles:midi/Dance/copper-list.mid"; fail=1; }
exit $fail
