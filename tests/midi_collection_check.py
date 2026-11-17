#!/usr/bin/env python3
"""The MIDI collection's own rules: every file in midi/ is named in midi/SOURCES.tsv and every row has its file; each
row gives a title, the licence Public Domain and where the piece came from; names fit an Amiga disk (30 characters,
plain ASCII, no two alike but for case).

    python3 tests/midi_collection_check.py [DIR]

MIT, Copyright (c) 2026 AmiCradle Team."""
import sys
from pathlib import Path


OURS = "AmiCradle Team"


def check(root: Path) -> list[str]:
    bad = []
    lines = (root / "SOURCES.tsv").read_text(encoding="utf-8").splitlines()
    head = lines[0].split("\t")
    if head[:5] != ["file", "title", "composer", "opus", "licence"]:
        bad.append("SOURCES.tsv: the first line isn't the column names")
    rows = {}
    for n, line in enumerate(lines[1:], 2):
        f = line.split("\t")
        if len(f) != len(head):
            bad.append(f"SOURCES.tsv line {n}: {len(f)} columns, not {len(head)}")
            continue
        r = dict(zip(head, f))
        if r["file"] in rows:
            bad.append(f"SOURCES.tsv line {n}: {r['file']} is there twice")
        rows[r["file"]] = r
        ours = r["composer"] == OURS            # a tune of our own, written by a script in tools/
        if ours and (r["licence"] != "MIT" or not r["mutopia source"].startswith("tools/") or r["mutopia page"]):
            bad.append(f"{r['file']}: a tune of our own is MIT, names its script in tools/ and has no Mutopia page")
        if not ours and r["licence"] != "Public Domain":
            bad.append(f"{r['file']}: licence {r['licence']!r}; only Public Domain pieces ship")
        if not r["title"] or not r["mutopia source"]:
            bad.append(f"{r['file']}: no title or no source")
    files = {str(p.relative_to(root)) for p in root.rglob("*.mid")}
    bad += [f"{f}: not in SOURCES.tsv" for f in sorted(files - set(rows))]
    bad += [f"{f}: in SOURCES.tsv, but no such file" for f in sorted(set(rows) - files)]
    seen = set()
    for f in sorted(files):
        for part in f.split("/"):
            if len(part) > 30 or not part.isascii() or any(c in part for c in ' :"*?#'):
                bad.append(f"{f}: {part!r} isn't a name for an Amiga disk")
        if f.count("/") != 1:
            bad.append(f"{f}: files go one drawer down, midi/<Composer>/")
        if f.lower() in seen:
            bad.append(f"{f}: the same name as another but for case")
        seen.add(f.lower())
    for p in root.iterdir():
        if p.is_file() and p.name not in ("SOURCES.tsv", "README.md"):
            bad.append(f"{p.name}: not part of the collection")
    return bad


def self_test() -> None:
    import tempfile
    with tempfile.TemporaryDirectory() as t:
        root = Path(t)
        head = "file\ttitle\tcomposer\topus\tlicence\tmutopia page\tmutopia source\ttypeset by\n"
        (root / "A").mkdir()
        (root / "A" / "x.mid").write_bytes(b"MThd")
        (root / "SOURCES.tsv").write_text(head + "A/x.mid\tT\tC\t1\tPublic Domain\tu\tftp/a.ly\tm\n")
        assert check(root) == [], check(root)
        (root / "A" / "y.mid").write_bytes(b"MThd")
        assert any("not in SOURCES" in b for b in check(root))
        (root / "A" / "y.mid").unlink()
        (root / "SOURCES.tsv").write_text(head + "A/x.mid\tT\tC\t1\tCreative Commons Attribution 3.0\tu\tftp/a.ly\tm\n")
        assert any("only Public Domain" in b for b in check(root))
        (root / "SOURCES.tsv").write_text(head + "A/x.mid\tT\tAmiCradle Team\t\tMIT\t\ttools/make.py\t\n")
        assert not check(root), check(root)             # a tune of our own
        (root / "SOURCES.tsv").write_text(head + "A/x.mid\tT\tAmiCradle Team\t\tMIT\tu\tftp/a.ly\t\n")
        assert any("a tune of our own" in b for b in check(root))
        (root / "SOURCES.tsv").write_text(head + "A/x.mid\tT\tC\t1\tPublic Domain\tu\tftp/a.ly\tm\nA/z.mid\tT\tC\t1\tPublic Domain\tu\tftp/a.ly\tm\n")
        assert any("no such file" in b for b in check(root))
        long = "n" * 27 + ".mid"
        (root / "A" / long).write_bytes(b"MThd")
        (root / "SOURCES.tsv").write_text(head + f"A/x.mid\tT\tC\t1\tPublic Domain\tu\tftp/a.ly\tm\nA/{long}\tT\tC\t1\tPublic Domain\tu\tftp/a.ly\tm\n")
        assert any("Amiga disk" in b for b in check(root))
    print("PASS: 5 cases of the collection's rules")


if __name__ == "__main__":
    self_test()
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "midi"
    bad = check(root)
    for b in bad:
        print("FAIL:", b)
    if bad:
        sys.exit(1)
    print(f"PASS: {len(list(root.rglob('*.mid')))} files, each with its source and licence")
