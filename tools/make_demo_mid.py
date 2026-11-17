#!/usr/bin/env python3
"""Writes demo/gfopensynth-demo.mid: a short tune of our own (16 bars: lead,
strings, bass, piano and drums) to hear GFOpenSynth with. General MIDI,
format 1, 96 ticks a beat. MIT, Copyright (c) 2026 AmiCradle Team.
Run from the top of the tree: python3 tools/make_demo_mid.py [--check]"""
import os, struct, sys

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(HERE, "demo/gfopensynth-demo.mid")
TPB = 96
BAR = 4 * TPB

def vlq(n):
    b = [n & 0x7F]
    n >>= 7
    while n:
        b.append((n & 0x7F) | 0x80)
        n >>= 7
    return bytes(reversed(b))

def track(events):
    """events: (tick, order, bytes); written in time order with delta times"""
    out, last = b"", 0
    for tick, _, data in sorted(events, key=lambda e: (e[0], e[1])):
        out += vlq(tick - last) + data
        last = tick
    out += vlq(0) + b"\xFF\x2F\x00"
    return b"MTrk" + struct.pack(">I", len(out)) + out

def note(ev, ch, tick, key, length, vel):
    ev.append((tick, 1, bytes([0x90 | ch, key, vel])))
    ev.append((tick + length, 0, bytes([0x80 | ch, key, 0])))

# the chords, a bar each: root (bass), then the triad around middle C
CHORDS = {"C": (36, (60, 64, 67)), "Am": (33, (57, 60, 64)), "F": (29, (57, 60, 65)), "G": (31, (59, 62, 67)),
          "Em": (28, (59, 64, 67)), "Dm": (26, (57, 62, 65))}
SONG = ["C", "Am", "F", "G", "C", "Am", "F", "G", "F", "G", "Em", "Am", "Dm", "G", "C", "C"]
# the lead: (beat in the bar in half beats, key offset from the chord's top note, length in half beats)
PHRASES = [
    [(0, 12, 2), (2, 9, 1), (3, 5, 1), (4, 7, 3), (7, 5, 1)],
    [(0, 5, 1), (1, 7, 1), (2, 9, 2), (4, 12, 2), (6, 9, 2)],
    [(0, 12, 3), (3, 14, 1), (4, 12, 2), (6, 7, 2)],
    [(0, 7, 2), (2, 5, 2), (4, 7, 4)],
]

def build():
    tempo = [(0, 0, b"\xFF\x51\x03" + struct.pack(">I", 60000000 // 112)[1:])]
    lead = [(0, 0, bytes([0xC0, 80])), (0, 0, bytes([0xB0, 7, 100])), (0, 0, bytes([0xB0, 10, 74]))]
    pad = [(0, 0, bytes([0xC1, 48])), (0, 0, bytes([0xB1, 7, 70])), (0, 0, bytes([0xB1, 10, 44]))]
    bass = [(0, 0, bytes([0xC2, 33])), (0, 0, bytes([0xB2, 7, 110]))]
    piano = [(0, 0, bytes([0xC3, 0])), (0, 0, bytes([0xB3, 7, 90])), (0, 0, bytes([0xB3, 10, 84]))]
    drums = []
    for bar, name in enumerate(SONG):
        t = bar * BAR
        root, triad = CHORDS[name]
        last = bar == len(SONG) - 1
        for k in triad:                                   # strings hold the chord
            note(pad, 1, t, k, BAR - 6, 70)
        for i in range(8):                                # bass: root and fifth in half beats
            if last and i: break
            note(bass, 2, t + i * 48, root + (7 if i in (3, 6) else 12 if i == 7 else 0), BAR if last else 40, 105)
        for i in range(8):                                # piano: the chord broken up and down
            if last: break
            note(piano, 3, t + i * 48, triad[(0, 1, 2, 1, 2, 1, 0, 1)[i]] + 12, 44, 72 if i % 2 else 84)
        if bar >= 4:                                      # the lead comes in at bar 5
            if last:
                note(lead, 0, t, triad[0] + 12, BAR, 100)
            else:
                for at, off, ln in PHRASES[(bar - 4) % 4]:
                    note(lead, 0, t + at * 48, triad[0] + off, ln * 48 - 6, 100)
        if last:
            note(drums, 9, t, 36, 24, 120); note(drums, 9, t, 49, 24, 110)
            continue
        for i in range(8):                                # hats in half beats, the last one open
            note(drums, 9, t + i * 48, 46 if i == 7 else 42, 20, 80 if i % 2 else 96)
        for b in (0, 5):
            note(drums, 9, t + b * 48, 36, 24, 118)       # kick
        for b in (2, 6):
            note(drums, 9, t + b * 48, 38, 24, 110)       # snare
        if bar % 4 == 0:
            note(drums, 9, t, 49, 24, 100)                # crash at each line
        if bar % 4 == 3:                                  # a tom run into the next line
            for i, k in enumerate((50, 47, 45, 41)):
                note(drums, 9, t + BAR - 96 + i * 24, k, 20, 100 + i * 4)
    tracks = [tempo, lead, pad, bass, piano, drums]
    return b"MThd" + struct.pack(">IHHH", 6, 1, len(tracks), TPB) + b"".join(track(t) for t in tracks)

if __name__ == "__main__":
    data = build()
    if "--check" in sys.argv:
        if not os.path.exists(OUT) or open(OUT, "rb").read() != data:
            sys.exit("demo/gfopensynth-demo.mid is not what tools/make_demo_mid.py writes")
    else:
        open(OUT, "wb").write(data)
        print("%s: %d bytes" % (OUT, len(data)))
