# Our own MT-32 engine

GFOpenSynth plays MT-32 music today by mapping each MT-32 sound to its
nearest General MIDI one (`GSE_MT32`, `GS_MODE_MT32_GM`). This is the plan
for a real MT-32 engine: Roland's LA synthesis, written by us from the
MT-32's published MIDI implementation and from how the instrument is
known to behave. No code from Munt, mt32-pi or anyone else goes in.

## Steps

| Step | What | Needs a ROM |
| --- | --- | --- |
| 1 | The MT-32's memory and the system exclusive messages that write it: patches, timbres, rhythm setup, system area, display, reset (`src/gs_mt32mem.c`). No sound. | no |
| 2 | One partial of the synthesiser kind: sawtooth and pulse waves with pulse width, the resonant filter, the pitch, filter and amplifier envelopes, the pitch LFO, key follow and bias. Whole-number arithmetic, as the rest of the engine. | no |
| 3 | Notes: the 13 structures that join partials in pairs (mix and ring modulation), assign modes, partial reserve, bender, key shift, pan and level. A timbre a song sends plays from here on. | no |
| 4 | The user's ROMs: the timbres, patches, rhythm setup and sampled waves (PCM partials) read from the control and PCM ROMs. | yes |
| 5 | Reverb (room, hall, plate, tap delay), master tune and volume, the display's text passed to the caller. | no |
| 6 | The engine in gfopensynth.library and behind AkikoPlus's MIDI port as what `GSE_MT32` plays; listening passes against a real unit's recordings. | no |

## ROMs

The Roland ROMs are not ours. They are never shipped, fetched, or put in
a repository, a release or a test. The engine looks for the user's own
in `Emulation:Roms`, then `DEVS:MT32` (or `ENV:GFOpenSynth/RomPath`), and
knows them by SHA-1 (`src/gs_rom.c`); the tests write stand-in ROMs of
their own.

Without ROMs:

- Until step 3, MT-32 music plays as now, mapped to General MIDI.
- From step 3, a timbre the song itself sends that uses only
  synthesiser partials plays on the real engine. The 128 built-in sounds
  and the rhythm set live in the ROM, and so do the sampled attacks many
  sounds start with, so those parts stay on the General MIDI mapping.
- With ROMs (step 4) everything plays on the real engine.

## Step 1 in detail

`struct gs_mt32mem` holds the areas at their published addresses
(`gs_mt32mem.h` has the table). `gs_mt32mem_sysex` takes a Roland DT1
message for model 16 and this unit (0x10), checks its checksum and
writes it, holding every value to its range; a message for another
maker, model or unit is left alone, and one with a bad checksum is
refused. `gs_mt32_timbre_decode` gives a timbre's 246 bytes by name.
The ranges are the manual's; step 4 checks them against a real unit's
ROM on the test machine that holds one.

## Step 2 in detail

`src/gs_la.c` plays one partial from the 58 settings step 1 decodes. It is
not yet joined to notes or to the library: step 3 does that.

- Waves: a sawtooth, and a pulse made of two sawtooths one late by the pulse
  width; both have their edges smoothed so high notes do not alias.
- Filter: a resonant low pass, cutoff from the setting, its key follow, its
  envelope and the envelope's velocity sense.
- Amplifier: level, velocity sense, the two bias points, and a five-stage
  envelope with sustain or without.
- Pitch: coarse, fine, key follow, the pitch envelope, a triangle LFO with
  the modulation wheel, and the bender.
- `tests/la_test.c` (30 checks) measures pitch by counting cycles, the
  pulse's duty, the filter's and amplifier's effect on power and peak, the
  envelope's timing and the note's end, and runs every setting at its lowest
  and highest under the sanitizers and as a 32-bit build.

Every curve here (times, levels, cutoff, depths) is our own fit to the
manual's description, not a measurement. Step 6 tunes them by ear against a
real unit. Not read yet: the envelopes' time key follow and velocity follow,
the filter's bias, the filter envelope's depth key follow and the pulse
width's velocity sense.

## Step 3 in detail

`src/gs_mt32.c` takes MIDI and the MT-32's system exclusive messages and
plays notes. It is not yet in the library: step 6 does that.

- A note has up to four partials, joined in pairs by the timbre's two
  structures: mixed, ring modulated, or the first partial with the ring.
- 32 partials in all. When they run out, the oldest note whose key is up
  goes first, then the oldest on a part holding more than its partial
  reserve, then the playing part's own oldest.
- Per part: program change, bender with the patch's range, key shift, fine
  tune, level, pan, volume, expression, modulation, hold pedal, all notes
  off. The rhythm part plays the keys whose rhythm setup names a timbre the
  song sent.
- Only timbres the song sent play: a write to a part's timbre temporary
  area, or a patch that names timbre memory. Sampled (PCM) partials are
  silent, and a part on a ROM timbre answers "not playable"
  (`gs_mt32_playable`), so the caller keeps playing it by the General MIDI
  map. Step 4 fills both in from the user's ROMs.
- `tests/mt32_test.c` (43 checks): pitch, structures, program changes,
  controllers, pan, the pedal, assign modes, running out of partials, the
  rhythm part, reset, and 6000 arbitrary messages under the sanitizers.

Our readings, to check against a real unit: the four assign modes (1 and 2
give a key one note, 3 and 4 let it ring over itself; 2 and 4 keep what is
sounding when partials run out), pan 0 as hard right, a rhythm timbre
sounding at middle C, and a patch's fine tune reaching only partials whose
bender switch is on.
