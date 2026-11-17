# The MIDI collection

Public domain music as Standard MIDI Files, to play through GFOpenSynth:
`PlaySynth Media:midi/Joplin/entertainer.mid`, or open a file in OpenPlay.
The GFOpenSynth part installs this drawer as `Media:midi` where a volume
named Media is mounted.

Every file was made by us with GNU LilyPond from a source file of the
Mutopia Project (https://www.mutopiaproject.org) whose own header places
it in the public domain; the composers are long out of copyright.
`SOURCES.tsv` names, for each file, the piece, its Mutopia page, the
source file it was made from and who typeset it. Only pieces marked
"Public Domain" are taken: Mutopia's Creative Commons pieces are left
out, so nothing here asks for a credit or a licence text to travel with
it. Our thanks to Mutopia's volunteers all the same.

Parts that rest for their first 20 seconds, and files over 200 KB, are
left out. `tests/midi_collection_check.py` and
`tests/midi_collection_test.c` hold the collection to these rules and
play every file through the engine.

## Dance

`Dance/` holds dance tunes of our own: four on the floor with a heavy kick
and bass, 124 to 130 beats a minute. `tools/make_dance_mid.py` writes them
from its tables, note by note; nothing is taken from anyone's tune. They
are ours, under this repository's MIT licence. GFOpenBlanker plays from
this drawer when its `PLAYMIDI` setting is on.
