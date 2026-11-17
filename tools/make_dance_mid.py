#!/usr/bin/env python3
"""Writes midi/Dance/*.mid: dance tunes of our own, for Media:midi/Dance
(GFOpenBlanker's music, OpenPlay). Four on the floor with a heavy kick and
bass, 124 to 130 beats a minute, General MIDI, format 1, 96 ticks a beat.
Every note comes from the tables below: nothing is taken from anyone's tune.
MIT, Copyright (c) 2026 AmiCradle Team.
Run from the top of the tree: python3 tools/make_dance_mid.py [--check]"""
import os, struct, sys

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(HERE, "midi/Dance")
TPB = 96
BAR = 4 * TPB
S = TPB // 4                                             # a sixteenth

def vlq(n):
    b = [n & 0x7F]
    n >>= 7
    while n:
        b.append((n & 0x7F) | 0x80)
        n >>= 7
    return bytes(reversed(b))

def track(events):
    out, last = b"", 0
    for tick, _, data in sorted(events, key=lambda e: (e[0], e[1])):
        out += vlq(tick - last) + data
        last = tick
    out += vlq(0) + b"\xFF\x2F\x00"
    return b"MTrk" + struct.pack(">I", len(out)) + out

def note(ev, ch, tick, key, length, vel):
    ev.append((tick, 1, bytes([0x90 | ch, key, vel])))
    ev.append((tick + length, 0, bytes([0x80 | ch, key, 0])))

MINOR = (0, 2, 3, 5, 7, 8, 10)

def degree(root, d):
    """the d-th step (0 is the root) of the natural minor scale on root"""
    return root + 12 * (d // 7) + MINOR[d % 7]

# A tune: its name and file, tempo, key (the bass's root), the four chords of its
# line as scale steps, the bass's sixteenths in a bar (0 the chord's root, 1 its
# octave, None a rest), the lead's sixteenths (steps above the chord's root, None
# a rest), and its instruments (General MIDI numbers from 0).
TUNES = [
    dict(file="night-drive.mid", title="Night Drive", bpm=126, root=33, chords=(0, 5, 2, 6),
         bass=(None, None, 0, None, None, None, 0, None, None, None, 0, None, None, None, 0, 1),
         lead=(0, None, 2, 4, None, 2, 7, None, 4, None, 2, 4, None, 7, 9, None),
         bass_prog=38, lead_prog=81, pad_prog=89),
    dict(file="pulse.mid", title="Pulse", bpm=128, root=38, chords=(0, 3, 5, 4),
         bass=(None, None, 0, 0, None, None, 0, 1, None, None, 0, 0, None, None, 1, 0),
         lead=(7, None, None, 4, None, None, 2, None, 4, None, None, 7, None, 9, None, 7),
         bass_prog=39, lead_prog=80, pad_prog=90),
    dict(file="copper-list.mid", title="Copper List", bpm=124, root=28, chords=(0, 0, 5, 6),
         bass=(0, None, 0, None, None, 1, 0, None, 0, None, 0, None, None, 1, None, 1),
         lead=(0, 2, 4, 7, 4, 2, 0, None, 2, 4, 7, 9, 7, 4, 2, None),
         bass_prog=38, lead_prog=62, pad_prog=50),
    dict(file="blitter-beat.mid", title="Blitter Beat", bpm=130, root=31, chords=(0, 6, 5, 4),
         bass=(None, None, 0, None, None, 0, 0, None, None, None, 0, None, 1, None, 0, 1),
         lead=(9, None, 7, None, 4, None, 7, 9, None, 7, None, 4, 2, None, 4, None),
         bass_prog=87, lead_prog=81, pad_prog=91),
]

# the tune's sections, in bars: what plays in each
PLAN = [("intro", 4), ("drums", 4), ("groove", 8), ("full", 16), ("break", 8), ("build", 4), ("full", 16), ("groove", 4), ("end", 1)]

def build(t):
    tempo = [(0, 0, b"\xFF\x51\x03" + struct.pack(">I", 60000000 // t["bpm"])[1:]),
             (0, 0, b"\xFF\x03" + vlq(len(t["title"])) + t["title"].encode("ascii"))]
    bass = [(0, 0, bytes([0xC0, t["bass_prog"]])), (0, 0, bytes([0xB0, 7, 127]))]
    lead = [(0, 0, bytes([0xC1, t["lead_prog"]])), (0, 0, bytes([0xB1, 7, 84])), (0, 0, bytes([0xB1, 10, 78]))]
    pad = [(0, 0, bytes([0xC2, t["pad_prog"]])), (0, 0, bytes([0xB2, 7, 62])), (0, 0, bytes([0xB2, 10, 50]))]
    drums = [(0, 0, bytes([0xB9, 7, 127]))]
    bar = 0
    for section, bars in PLAN:
        for i in range(bars):
            at = bar * BAR
            cs = t["chords"][(bar // 2) % 4] if section != "intro" else 0     # the chord, as a step of the key
            chord = degree(t["root"], cs)
            kick = section in ("drums", "groove", "full", "build")
            if section == "end":                         # one last hit, held
                note(drums, 9, at, 36, 24, 127); note(drums, 9, at, 49, 24, 120)
                note(bass, 0, at, t["root"], BAR, 127)
                for k in (0, 2, 4):
                    note(pad, 2, at, degree(t["root"], k) + 24, BAR, 90)
                break
            if kick:                                     # the heavy beat: two kicks together on every beat
                for b in range(4):
                    note(drums, 9, at + b * TPB, 36, 24, 127)
                    note(drums, 9, at + b * TPB, 35, 24, 120)
            if section in ("groove", "full"):
                for b in (1, 3):                         # clap and snare on two and four
                    note(drums, 9, at + b * TPB, 39, 24, 112)
                    note(drums, 9, at + b * TPB, 38, 24, 96)
                for b in range(4):                       # open hat between the beats
                    note(drums, 9, at + b * TPB + 2 * S, 46, 20, 100)
            if section in ("intro", "drums", "full", "build"):
                for s in range(16):                      # closed hats in sixteenths
                    if s % 4 != 2 or section != "full":
                        note(drums, 9, at + s * S, 42, 10, 84 if s % 4 == 0 else 60)
            if section == "build":                       # snare roll, faster and louder, into the drop
                step = S if i >= bars - 2 else 2 * S
                for s in range(0, 16 * S, step):
                    note(drums, 9, at + s, 38, 10, 60 + (i * 16 * S + s) * 60 // (bars * 16 * S))
            if section in ("full", "groove") and i == 0:
                note(drums, 9, at, 49, 24, 110)          # crash at the top
            if section != "break":                       # the bass, loud, on the off beats
                for s, b in enumerate(t["bass"]):
                    if b is not None and (section != "intro" or s % 4 == 2):
                        note(bass, 0, at + s * S, chord + 12 * b, S * 2 - 4 if b == 0 else S - 2, 124)
            if section in ("full", "break", "build"):
                for k in (0, 2, 4):                      # the chord, held
                    note(pad, 2, at, degree(t["root"], cs + k) + 24, BAR - 6, 70 if section == "full" else 88)
            if section in ("full", "break"):
                for s, d in enumerate(t["lead"]):
                    if d is not None and (section == "full" or s % 2 == 0):
                        note(lead, 1, at + s * S, degree(t["root"], cs + d) + 36, S - 2, 104 if s % 4 == 0 else 88)
            bar += 1
    tracks = [tempo, bass, lead, pad, drums]
    return b"MThd" + struct.pack(">IHHH", 6, 1, len(tracks), TPB) + b"".join(track(x) for x in tracks)

if __name__ == "__main__":
    for t in TUNES:
        data, path = build(t), os.path.join(OUT, t["file"])
        if "--check" in sys.argv:
            if not os.path.exists(path) or open(path, "rb").read() != data:
                sys.exit("midi/Dance/%s is not what tools/make_dance_mid.py writes" % t["file"])
        else:
            os.makedirs(OUT, exist_ok=True)
            open(path, "wb").write(data)
            print("%s: %d bytes" % (path, len(data)))
