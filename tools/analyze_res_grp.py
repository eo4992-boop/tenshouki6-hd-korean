#!/usr/bin/env python3
"""Read-only forensic scanner for NOBU6HD resource containers.

Usage:
  python tools/analyze_res_grp.py "D:/Game/res/res_grp.bin" --out res_grp_report

The script never modifies the input. It emits small text/JSON reports only.
"""
from __future__ import annotations
import argparse, hashlib, json, math, os, re, struct
from pathlib import Path

PRINTABLE = re.compile(rb"[ -~]{5,}")

def entropy(data: bytes) -> float:
    if not data:
        return 0.0
    counts = [0] * 256
    for b in data: counts[b] += 1
    n = len(data)
    return -sum((c/n) * math.log2(c/n) for c in counts if c)

def u32s(data: bytes, step=4, limit=200000):
    vals=[]
    end=min(len(data)-3, limit*step)
    for off in range(0, max(0,end+1), step):
        vals.append((off, struct.unpack_from('<I', data, off)[0]))
    return vals

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('input', type=Path)
    ap.add_argument('--out', type=Path, default=None)
    ap.add_argument('--sample-mb', type=float, default=1.0)
    args=ap.parse_args()
    src=args.input
    if not src.is_file(): raise SystemExit(f"Input file not found: {src}")
    out=args.out or src.with_name(src.name + '_report')
    out.mkdir(parents=True, exist_ok=True)
    size=src.stat().st_size
    h=hashlib.sha256()
    with src.open('rb') as f:
        while True:
            b=f.read(1024*1024)
            if not b: break
            h.update(b)
    sample_n=max(4096, int(args.sample_mb*1024*1024))
    with src.open('rb') as f:
        head=f.read(sample_n)
        if size > sample_n:
            f.seek(max(0,size-sample_n)); tail=f.read(sample_n)
        else: tail=head
    report={
      'path': str(src), 'size': size, 'sha256': h.hexdigest(),
      'head_entropy': round(entropy(head),5), 'tail_entropy': round(entropy(tail),5),
      'head_hex': head[:256].hex(' '), 'tail_hex': tail[-256:].hex(' '),
      'possible_signatures': [], 'ascii_strings': [], 'utf16le_strings': [],
      'u32_candidates': []
    }
    for label, blob, base in [('head',head,0),('tail',tail,max(0,size-len(tail)))]:
        for m in PRINTABLE.finditer(blob):
            s=m.group().decode('ascii','replace')
            report['ascii_strings'].append({'region':label,'offset':base+m.start(),'text':s[:300]})
        # UTF-16LE strings, useful for resource metadata.
        for m in re.finditer(rb'(?:[ -~]\x00){5,}', blob):
            raw=m.group(); s=raw.decode('utf-16le','replace').rstrip('\x00')
            report['utf16le_strings'].append({'region':label,'offset':base+m.start(),'text':s[:300]})
        for sig in (b'LS11', b'RIFF', b'PK\x03\x04', b'\x78\x9c', b'\x78\xda', b'OggS', b'DDS ', b'BM', b'\x89PNG'):
            pos=blob.find(sig)
            if pos >= 0: report['possible_signatures'].append({'signature':sig.decode('latin1'),'offset':base+pos,'region':label})
        # Detect runs of plausible little-endian offsets into the file.
        vals=u32s(blob)
        for i in range(len(vals)-8):
            window=[v for _,v in vals[i:i+8]]
            if all(0 <= v < size for v in window) and all(window[j] <= window[j+1] for j in range(7)) and len(set(window)) >= 6:
                report['u32_candidates'].append({'region':label,'offset':base+vals[i][0],'values':window})
                if len(report['u32_candidates']) >= 100: break
    # Deduplicate and bound report size.
    for key in ('ascii_strings','utf16le_strings','possible_signatures','u32_candidates'):
        seen=set(); clean=[]
        for item in report[key]:
            k=json.dumps(item,sort_keys=True)
            if k not in seen: seen.add(k); clean.append(item)
        report[key]=clean[:1000]
    (out/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    lines=[
      f"Input: {src}", f"Size: {size:,} bytes", f"SHA-256: {h.hexdigest()}",
      f"Head entropy: {report['head_entropy']}", f"Tail entropy: {report['tail_entropy']}",
      "", "Possible signatures:", *[f"  0x{x['offset']:X}: {x['signature']} ({x['region']})" for x in report['possible_signatures']],
      "", "ASCII strings:", *[f"  0x{x['offset']:X}: {x['text']}" for x in report['ascii_strings']],
      "", "UTF-16LE strings:", *[f"  0x{x['offset']:X}: {x['text']}" for x in report['utf16le_strings']],
      "", "Plausible offset-table candidates:", *[f"  0x{x['offset']:X}: {x['values']}" for x in report['u32_candidates']],
    ]
    (out/'report.txt').write_text('\n'.join(lines),encoding='utf-8')
    print(f"Wrote: {out/'report.txt'}")
    print(f"Wrote: {out/'report.json'}")

if __name__ == '__main__': main()
