#!/usr/bin/env python3
"""Read-only analyzer for suspected game data files."""
from __future__ import annotations
import argparse, hashlib, json, math
from pathlib import Path


def entropy(data: bytes) -> float:
    if not data:
        return 0.0
    counts = [0] * 256
    for b in data:
        counts[b] += 1
    n = len(data)
    return -sum((c / n) * math.log2(c / n) for c in counts if c)


def ascii_candidates(data: bytes, minimum: int = 4):
    out, start = [], None
    for i, b in enumerate(data + b"\x00"):
        printable = 32 <= b <= 126 or b in (9,)
        if printable and start is None:
            start = i
        elif not printable and start is not None:
            if i - start >= minimum:
                out.append({"offset": start, "length": i - start, "text": data[start:i].decode("ascii", "replace")})
            start = None
    return out


def analyze(path: Path, preview: int):
    if not path.is_file():
        raise ValueError(f"not a regular file: {path}")
    data = path.read_bytes()
    return {
        "path": str(path),
        "size": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
        "header_hex": data[:preview].hex(" "),
        "entropy_bits_per_byte": round(entropy(data), 6),
        "ascii_candidates": ascii_candidates(data),
    }


def main():
    p = argparse.ArgumentParser()
    p.add_argument("file", type=Path)
    p.add_argument("-o", "--output", type=Path)
    p.add_argument("--preview", type=int, default=64)
    args = p.parse_args()
    try:
        result = analyze(args.file, max(0, args.preview))
    except (OSError, ValueError) as exc:
        p.error(str(exc))
    text = json.dumps(result, ensure_ascii=False, indent=2) + "\n"
    if args.output:
        args.output.write_text(text, encoding="utf-8", newline="\n")
    else:
        print(text, end="")


if __name__ == "__main__":
    main()
