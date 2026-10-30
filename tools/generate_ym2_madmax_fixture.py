#!/usr/bin/env python3
"""Build deterministic, synthetic YM2 fixtures for Mad Max trigger testing.

These files intentionally contain NO copyrighted sample data. They exercise
R10 bit-7 event recognition, *not* proof of audible digi-drum reproduction.
"""
import argparse
from pathlib import Path

FRAMES = 50


def create(include_triggers=True):
    regs = [bytearray(FRAMES) for _ in range(14)]
    for i in range(FRAMES):
        regs[0][i] = 250
        regs[7][i] = 0x3e
        regs[8][i] = 15
        regs[13][i] = 0xff
    if include_triggers:
        for frame, sample, timer in ((5, 1, 20), (15, 2, 32), (25, 3, 48)):
            regs[10][frame] = 0x80 | sample
            regs[12][frame] = timer
    return b"YM2!" + b"".join(regs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    args.directory.mkdir(parents=True, exist_ok=True)
    flagged = args.directory / "ym2_madmax_triggers.ym"
    plain = args.directory / "ym2_no_madmax.ym"
    flagged.write_bytes(create(True))
    plain.write_bytes(create(False))
    from probe_ym2_madmax import inspect
    frames, events = inspect(flagged.read_bytes())
    assert frames == FRAMES
    assert [(f, sample, timer) for f, sample, timer, _ in events] == [
        (5, 1, 20), (15, 2, 32), (25, 3, 48)]
    assert inspect(plain.read_bytes())[1] == []
    print("PASS: YM2 positive/negative Mad Max trigger fixtures")
    print(flagged)
    print(plain)


if __name__ == "__main__":
    main()
