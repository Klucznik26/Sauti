#!/usr/bin/env python3
"""Convert a local asapscan -d text dump to a diagnostic PAL SAP-R.
One row per video frame, NOT per player call. No third-party code or music
included. Keep generated music fixtures outside version control.
"""
import argparse
from pathlib import Path
import re
from decimal import Decimal

ROW = re.compile(r"\s*(\d+\.\d{2}):\s*(.*?)\s*")
BYTE = re.compile(r"[0-9A-Fa-f]{2}")
CLOCK = 1773447
CYCLES = 312 * 114

def convert(lines, limit):
    payload = bytearray()
    chips = None
    count = 0
    for line in lines:
        if not line.strip():
            continue
        m = ROW.fullmatch(line)
        if not m:
            raise ValueError(f"unexpected dump line {count+1}: {line[:100]!r}")
        groups = [g.split() for g in m[2].split("|")]
        if len(groups) not in (1, 2) or any(
            len(g) != 9 or any(not BYTE.fullmatch(b) for b in g)
            for g in groups
        ):
            raise ValueError(f"invalid register groups in row {count+1}")
        if chips is None:
            chips = len(groups)
        if len(groups) != chips:
            raise ValueError("channel count changed")
        expected = Decimal(count * CYCLES) / CLOCK
        if abs(Decimal(m[1])-expected) > Decimal("0.011"):
            raise ValueError(f"row {count+1}: timestamp inconsistent with PAL video frames")
        payload.extend(int(b, 16) for g in groups for b in g)
        count += 1
        if count == limit:
            break
    if not count:
        raise ValueError("empty dump")
    if limit is not None and count != limit:
        raise ValueError("dump shorter than requested")
    header = b"SAP\r\nTYPE R\r\nFASTPLAY 312\r\n"
    if chips == 2:
        header += b"STEREO\r\n"
    return header + b"\r\n" + payload, count, chips

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("dump", type=Path)
    p.add_argument("output", type=Path)
    p.add_argument("--frames", type=int, default=1500)
    a = p.parse_args()
    if not 1 <= a.frames <= 30000:
        p.error("--frames must be 1..30000")
    if a.dump.resolve() == a.output.resolve():
        p.error("input and output must differ")
    try:
        with a.dump.open(encoding="ascii") as f:
            data, n, chips = convert(f, a.frames)
        a.output.write_bytes(data)
    except (OSError, ValueError) as e:
        p.error(str(e))
    print(f"{a.output}: {n} register frames, {chips} channel(s), "
          f"{n*CYCLES/CLOCK:.3f}s, "
          f"{n*CYCLES*48000//CLOCK} expected PCM frames at 48000 Hz")

if __name__ == "__main__":
    main()
