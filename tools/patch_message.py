#!/usr/bin/env python3
"""Safely replace one CP932 message in a table-based N6 message file.

The file is copied to a new output path; the source is never modified.
"""
from __future__ import annotations
import argparse
from pathlib import Path


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("source", type=Path)
    p.add_argument("output", type=Path)
    p.add_argument("--index", type=int, required=True, help="zero-based message index")
    p.add_argument("--text", required=True, help="replacement text encodable as CP932")
    args = p.parse_args()

    raw = args.source.read_bytes()
    if len(raw) < 202:
        raise SystemExit("file is too small for the expected N6 message table")
    count = int.from_bytes(raw[:2], "little")
    if not 0 <= args.index < count:
        raise SystemExit(f"index must be between 0 and {count-1}")
    offsets = [int.from_bytes(raw[2+i*2:4+i*2], "little") for i in range(count)]
    data_start = 2 + count * 2
    if offsets[0] < data_start or any(b < a for a, b in zip(offsets, offsets[1:])):
        raise SystemExit("offset table is not monotonic or has an invalid first offset")
    start = offsets[args.index]
    end = offsets[args.index + 1] if args.index + 1 < count else len(raw)
    encoded = args.text.encode("cp932")
    old = raw[start:end]
    if len(encoded) > len(old):
        raise SystemExit(f"replacement is too long: {len(encoded)} bytes, available {len(old)} bytes")
    replacement = encoded + b"\x00" * (len(old) - len(encoded))
    out = bytearray(raw)
    out[start:end] = replacement
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(out)
    print(f"written: {args.output}")
    print(f"message_index: {args.index}")
    print(f"old_bytes: {len(old)} new_bytes: {len(encoded)} padded: {len(old)-len(encoded)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
