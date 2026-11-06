#!/usr/bin/env python3
"""SBR-55: fail-closed comparison of the five terms that form QMF z[0]."""
import argparse
import array
import math
import sys
from pathlib import Path


def read_rows(path):
    raw = Path(path).read_bytes()
    if not raw or len(raw) % 20:
        raise ValueError(f"{path}: expected nonempty five-float rows")
    values = array.array("f")
    values.frombytes(raw)
    if sys.byteorder != "little":
        values.byteswap()
    if not all(math.isfinite(value) for value in values):
        raise ValueError(f"{path}: nonfinite float")
    return [list(values[index:index + 5]) for index in range(0, len(values), 5)]


def compare(sauti, ffmpeg, start):
    if len(sauti) != len(ffmpeg) or start >= len(sauti):
        raise ValueError("row count/start mismatch")
    result = []
    for term in range(5):
        delta = [left[term] - right[term] for left, right in zip(sauti[start:], ffmpeg[start:])]
        result.append((term * 64, max(abs(value) for value in delta),
                       math.sqrt(math.fsum(value * value for value in delta) / len(delta))))
    return result


def self_test():
    rows = [[float(slot + term) for term in range(5)] for slot in range(80)]
    changed = [row[:] for row in rows]
    changed[70][3] += 0.25
    result = compare(rows, changed, 64)
    assert result[3][0] == 192 and result[3][1] == 0.25
    assert all(entry[1] == 0.0 for entry in result if entry[0] != 192)
    print("PASS SBR-55 branch-0 audit: five-term rows, stable start, localization")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sauti")
    parser.add_argument("ffmpeg")
    parser.add_argument("--start-slot", type=int, default=64)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if args.start_slot < 0:
        parser.error("--start-slot must be >=0")
    try:
        result = compare(read_rows(args.sauti), read_rows(args.ffmpeg), args.start_slot)
    except (OSError, ValueError) as exc:
        parser.exit(2, f"SBR-55 branch-0 audit rejected: {exc}\n")
    print("SBR-55 branch-0 audit: diagnostic only; no decoder correction")
    for offset, maximum, rms in result:
        print(f"history_offset={offset} max_abs_delta={maximum:.9g} rms_delta={rms:.9g}")


if __name__ == "__main__":
    main()
