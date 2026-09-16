#!/usr/bin/env python3
"""Extract table-based N6 message files to UTF-8 TSV without modifying inputs."""
from __future__ import annotations
import argparse
import csv
from pathlib import Path


def extract(path: Path):
    raw = path.read_bytes()
    if len(raw) < 4:
        raise ValueError("file is too small")
    count = int.from_bytes(raw[:2], "little")
    table_end = 2 + count * 2
    if table_end > len(raw):
        raise ValueError("offset table exceeds file size")
    offsets = [int.from_bytes(raw[2+i*2:4+i*2], "little") for i in range(count)]
    if not offsets or offsets[0] < table_end or any(b < a for a, b in zip(offsets, offsets[1:])):
        raise ValueError("unsupported or invalid offset table")
    for i, start in enumerate(offsets):
        end = offsets[i + 1] if i + 1 < count else len(raw)
        payload = raw[start:end].rstrip(b"\x00")
        yield i, payload.decode("cp932", errors="replace")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("source", type=Path)
    ap.add_argument("output", type=Path)
    args = ap.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8-sig", newline="") as f:
        w = csv.writer(f, delimiter="\t", lineterminator="\n")
        w.writerow(["index", "text"])
        for index, text in extract(args.source):
            w.writerow([index, text])
    print(f"written: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
