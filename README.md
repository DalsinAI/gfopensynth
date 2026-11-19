# GFOpenSynth

MT-32 and General MIDI music for Amiga programs, without the original box.
Our own synth, written from the published specifications, in integer
arithmetic only, so one source runs:

- in host space behind **AkikoPlus's MIDI port**, as the device plugged into
  it, playing to the host's audio as a real MT-32 or SC-55 would;
- on other platforms;
- on a 68040 with no FPU, when there is no board (General MIDI only).

On the Amiga, `gfopensynth.library` and a CAMD driver send MIDI to the port,
so programs play as if a real synth were plugged in.

Status: the engine and the library (this draft). The AkikoPlus MIDI port
(guest device and host side) and the CAMD driver follow; see
[docs/gfopensynth.md](docs/gfopensynth.md).

- `library/`: gfopensynth.library: `GSOpenSynthA` picks the port
  (`akikomidi.device`, or `ENV:GFOpenSynth/Port`), else the 68k engine to
  AHI, else the caller's buffer (`GSRenderS16`, for programs that mix their
  own). Short messages, SysEx, raw MIDI bytes, MIDI files from disk or
  memory (`GSPlayFileA`, `GSPlayMemA`, looped or not; pause, seek, position),
  volume, reset. `C:PlaySynth FILE/A,MT32/S,LOOP/S,OUTPUT/K,SOUNDFONT/K,...`.
- `sh build-amiga.sh` (STOVE= or PREFIX= and NDK_INC=): `lib/gfopensynth.library`
  (no floating point, nothing from a C library), `lib/libgfopensynth.a`
  (`<gfopensynth.h>`, `-lgfopensynth`), `c/PlaySynth`.

- `soundfonts/basic.sf2` (built by `tools/make_basic_sf2.c`): GFOpenSynth
  Basic, our own small General MIDI SoundFont (129 presets, under 200 KB),
  every sample made from arithmetic, so music plays where no other SoundFont
  is installed. The library uses `DEVS:SoundFonts/default.sf2` when there is
  one (any General MIDI SoundFont, copied there by the user), else Basic.
- `src/gs_mt32mem.c`: the MT-32's memory and its system exclusive messages, step 1 of our own MT-32 engine
- `src/gs_la.c`: one partial of the synthesiser kind (waves, filter, envelopes, LFO), step 2 of our own MT-32 engine
- `src/gs_mt32.c`: notes from the timbres a song sends (partial pairs, ring modulation, parts, 32 partials), step 3 of our own MT-32 engine
  (`docs/MT32-ENGINE.md`); not yet used by the library.
- `midi/`: the MIDI collection, 700 public domain pieces made from the Mutopia Project's public domain
  sources, each with its source in `midi/SOURCES.tsv` (`midi/README.md`). The part installs it as `MediaFiles:midi`
  where a volume named MediaFiles is mounted.
- `demo/gfopensynth-demo.mid`: a tune of our own (`tools/make_demo_mid.py`).
  `tools/smf2wav.c` plays a MIDI file into a WAV on a PC with the same engine.
- `sh package.sh OUT DEST VERSION`: the release files (the GFOpenUp part
  `Parts/GFOpenSynth`, the SDK, SHA256SUMS).

- `src/gs_midi.c`: the port's byte stream parser (running status, SysEx,
  real-time bytes) and a Standard MIDI File reader and player
- `src/gs_sf2.c`: SoundFont 2 reader (SF2 2.04), in place, no copies
- `src/gs_synth.c`: the GM synth: voices, volume envelope, pan, bend and its
  range, sustain, exclusive classes, voice stealing, GM/GS reset, and an
  MT-32 mode that maps MT-32 programs to GM ones
- `src/gs_rom.c`: MT-32 / CM-32L ROMs known by SHA-1 (names never matter),
  halves joined, a complete set picked. Checksums only: no ROM is ever in
  this repository, a release or a build.
- `sh tests/run_host_tests.sh`: the PC tests (with AddressSanitizer and
  UBSan). They write their own tiny SoundFont and use made-up ROM bytes.

Designed, tested and delivered by The AmiCradle Team.

MIT, Copyright (c) 2026 AmiCradle Team.
