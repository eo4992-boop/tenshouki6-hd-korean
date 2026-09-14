#!/usr/bin/env python3
"""Read-only exploratory analyzer for N6P message containers.

This tool deliberately does not claim to decode the format. It reports byte
statistics, header candidates, and plausible little-endian offset/length
values for subsequent manual validation.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import struct
from pathlib import Path


def entropy(data: bytes) -> float:
    if not data:
        return 0.0
    counts = [0] * 256
    for value in data:
        counts[value] += 1
    size = len(data)
    return -sum((count / size) * math.log2(count / size) for count in counts if count)


def u32le(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def plausible_values(data: bytes, start: int, end: int) -> list[dict[str, int]]:
    result = []
    limit = min(end, len(data) - 3)
    for offset in range(max(0, start), limit + 1, 4):
        value = u32le(data, offset)
        if 0 < value < len(data):
            result.append({"field_offset": offset, "value": value})
    return result


def analyze(path: Path) -> dict:
    data = path.read_bytes()
    return {
        "path": str(path),
        "size": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
        "header_hex": data[:64].hex(" "),
        "entropy_bits_per_byte": round(entropy(data), 6),
        "little_endian_u32": [u32le(data, i) for i in range(0, min(64, len(data) - 3), 4)],
        "plausible_u32_fields_first_256": plausible_values(data, 0, 256),
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("file", type=Path)
    parser.add_argument("-o", "--output", type=Path)
    args = parser.parse_args()
    if not args.file.is_file():
        parser.error(f"not a regular file: {args.file}")
    result = json.dumps(analyze(args.file), ensure_ascii=False, indent=2) + "\n"
    if args.output:
        args.output.write_text(result, encoding="utf-8", newline="\n")
    else:
        print(result, end="")


if __name__ == "__main__":
    main()
