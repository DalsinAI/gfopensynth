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

Status: the engine (this draft). The library, the CAMD driver and the port's
host side follow; see [docs/gfopensynth.md](docs/gfopensynth.md).

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
