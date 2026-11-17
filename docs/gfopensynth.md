# GFOpenSynth design

MT-32 and General MIDI for Amiga programs. Studied from mt32-pi (a Pi that
stands in for an MT-32 or a SoundFont synth on a real MIDI port); none of its
code, Munt's or FluidSynth's is used. Ours, so it ports to other platforms.

## Shape

```
 Amiga program --CAMD--> DEVS:Midi/akikoplus --+
 Amiga program --gfopensynth.library-----------+--> AkikoPlus MIDI port (bytes)
                                                         |
                                  host space:  gfopensynth engine (src/)
                                               SoundFont / MT-32 ROMs from cards/
                                                         |
                                                   host audio out
```

- **The port.** AkikoPlus gets a MIDI port: a serial-style byte stream (out
  and in FIFOs, status), as a real MIDI interface is. Only MIDI bytes cross
  it; no PCM comes back, as with a real box.
- **The synth.** In host space, the device plugged into the port: the bytes
  go through `gs_synth_port` (parser + synth) and the synth renders to the
  host's audio, as a real synth's own audio out.
- **No board.** gfopensynth.library runs the same engine on the 68k
  (General MIDI from a SoundFont) and sends it to ahi.device, or hands the
  samples to a program that mixes its own (allegro4.library, ACGame).
- **ROMs and SoundFonts** live in host space under the card's folder, never
  in the guest, a repository, a release or a build. On the 68k rung the
  SoundFont comes from `DEVS:SoundFonts/` (or `ENV:GFOpenSynth/SoundFont`).

## Engines

1. **General MIDI from a SoundFont** (this draft): SF2 2.04 zones, the
   volume envelope (delay, attack, hold, decay, sustain, release, key
   scaling), pan, initial attenuation (0.4 of the specification's unit, as
   SoundFonts made on EMU's hardware expect), velocity, CC 7/10/11/64,
   RPN 0 bend range, all notes/sound off, reset controllers, exclusive
   classes, loops (none, always, until released), voice stealing (quietest
   released, else oldest). Not yet: the filter, the modulation envelope and
   LFOs, reverb and chorus, SF2 modulators.
2. **MT-32 through GM** (this draft): `GS_MODE_MT32_GM` listens on MIDI
   channels 2-10 as an MT-32 does from power on and maps its 128 programs to
   the nearest GM ones. MT-32 SysEx is counted and ignored.
3. **MT-32 (LA synthesis) from the ROMs**: ours, a later stage. The ROM
   handling it needs is here: `gs_rom.c` knows every MT-32, CM-32L and
   CM-32LN control ROM and both PCM ROMs by SHA-1, joins `_a`/`_b` (even and
   odd bytes) and `_l`/`_h` (halves) and the CM-32L PCM (MT-32 PCM followed
   by `cm32l_pcm_h`), and picks a complete set, newest control ROM first.

## Numbers

Everything is integer: Q16 pitch increments from a 2^(c/1200) table, Q15
gains from a 10^(-cB/200) table, the SF2 concave curve and constant-power pan
as tables (`tools/gen_tables.py` writes `src/gs_tables.c`; the host tests
check it is current). Envelopes and gains step every 32 frames
(`GS_BLOCK`); samples are linearly interpolated. 64-bit sums only at note
on, bend and tempo changes.

## Next

1. gfopensynth.library: open a synth (port, else 68k to AHI, else the
   caller's buffer), short messages, SysEx, SMF playback, volume, reset.
2. AkikoPlus MIDI port: guest device and the host side, in
   gfosautoconfigboards (`cards/akikoplus`) and the runtime, with this
   engine built for the host.
3. CAMD driver for the port.
4. allegro4.library's MIDI through it (the "MIDI is silent" gap).
5. The LA engine.

Labs wait until after the Pi 5 update and Dale's go. Tests with real ROMs run
only on our own machines.

MIT, Copyright (c) 2026 AmiCradle Team.
