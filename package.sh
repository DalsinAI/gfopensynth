#!/bin/sh
# The release files from a build-amiga.sh OUT:
#   gfopensynth-VERSION-part.tar.gz   Parts/GFOpenSynth: the GFOpenUp part, with the MIDI collection for Media:midi
#                                     (GFOpenUpTool INSTALL Parts/GFOpenSynth)
#   gfopensynth-VERSION-sdk.tar.gz    include/, libgfopensynth.a, docs, README, LICENSE
#   SHA256SUMS
#   package.sh OUT DEST VERSION
# MIT, Copyright (c) 2026 AmiCradle Team.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
OUT=$1 DEST=$2 VERSION=$3
for f in lib/gfopensynth.library lib/libgfopensynth.a c/PlaySynth soundfonts/basic.sf2 demo/gfopensynth-demo.mid; do
    [ -f "$OUT/$f" ] || { echo "$OUT/$f is missing: run build-amiga.sh first" >&2; exit 1; }
done
mkdir -p "$DEST"
DEST=$(CDPATH= cd -- "$DEST" && pwd)
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT

P="$T/part/Parts/GFOpenSynth"
mkdir -p "$P/Libs" "$P/C" "$P/Devs/SoundFonts" "$P/Demo"
cp "$OUT/lib/gfopensynth.library" "$P/Libs/gfopensynth.library"
cp "$OUT/c/PlaySynth" "$P/C/PlaySynth"
cp "$OUT/soundfonts/basic.sf2" "$P/Devs/SoundFonts/basic.sf2"
cp "$OUT/demo/gfopensynth-demo.mid" "$P/Demo/gfopensynth-demo.mid"
cat > "$P/Part" <<PART
part GFOpenSynth $VERSION
; gfopensynth.library: General MIDI and MT-32 music (AkikoPlus's MIDI port, or AHI)
needs 68040
add "Libs/gfopensynth.library" "LIBS:gfopensynth.library"
add "C/PlaySynth" "C:PlaySynth"
add "Devs/SoundFonts/basic.sf2" "DEVS:SoundFonts/basic.sf2"
add "Demo/gfopensynth-demo.mid" "DEVS:SoundFonts/gfopensynth-demo.mid"
PART
# The MIDI collection (midi/, public domain pieces, each with its source in SOURCES.tsv) goes to Media:midi where
# a volume named Media is mounted (the standard build's DH4:; GFOpenUpTool 0.8.22's "when MediaDrive"). An older
# GFOpenUpTool leaves these lines out.
mkdir -p "$P/Media"
cp -R "$HERE/midi" "$P/Media/midi"
(cd "$P" && find Media/midi -type f | LC_ALL=C sort) | while IFS= read -r f; do
    printf 'when MediaDrive add "%s" "Media:%s"\n' "$f" "${f#Media/}" >> "$P/Part"
done
(cd "$T/part" && tar -czf "$DEST/gfopensynth-$VERSION-part.tar.gz" --owner=0 --group=0 Parts)

S="$T/sdk/gfopensynth-$VERSION"
mkdir -p "$S/lib"
cp -R "$OUT/include" "$S/include"
cp "$OUT/lib/libgfopensynth.a" "$S/lib/libgfopensynth.a"
cp -R "$HERE/docs" "$S/docs"
cp "$HERE/README.md" "$HERE/LICENSE" "$S/"
(cd "$T/sdk" && tar -czf "$DEST/gfopensynth-$VERSION-sdk.tar.gz" --owner=0 --group=0 "gfopensynth-$VERSION")

(cd "$DEST" && sha256sum "gfopensynth-$VERSION-part.tar.gz" "gfopensynth-$VERSION-sdk.tar.gz" > SHA256SUMS && cat SHA256SUMS)
