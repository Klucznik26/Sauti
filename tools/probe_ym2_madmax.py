#!/usr/bin/env python3
"""Inspect raw YM2 register dumps for ST-Sound Mad Max digi-drum triggers.

A YM2 trigger is R10 bit 7; sample index = R10 & 0x7f.
ST-Sound starts the fixed external sample when R12 is nonzero,
at MFP_CLOCK / R12 Hz. This tool only inspects events; it does not
embed or reproduce third-party samples.
"""
import argparse
from collections import Counter
from pathlib import Path

MFP_CLOCK = 2457600


def inspect(data):
    if not data.startswith(b"YM2!"):
        raise ValueError("Expected raw YM2! data (unpack LHA first)")
    payload = memoryview(data)[4:]
    if not payload or len(payload) % 14:
        raise ValueError("Invalid YM2 register payload length")
    frames = len(payload) // 14
    r10 = payload[10 * frames:11 * frames]
    r12 = payload[12 * frames:13 * frames]
    events = []
    for frame in range(frames):
        if r10[frame] & 0x80:
            timer = int(r12[frame])
            events.append((frame, int(r10[frame] & 0x7f), timer,
                           MFP_CLOCK / timer if timer else None))
    return frames, events


def self_test():
    frames = 4
    planes = [bytearray(frames) for _ in range(14)]
    planes[10][:] = bytes([0, 0x85, 0x81, 0x7f])
    planes[12][:] = bytes([0, 10, 0, 25])
    count, events = inspect(b"YM2!" + b"".join(planes))
    assert count == frames
    assert events == [(1, 5, 10, 245760.0), (2, 1, 0, None)]
    try:
        inspect(b"YM2!broken")
    except ValueError:
        pass
    else:
        raise AssertionError("Invalid data accepted")
    print("PASS: YM2 Mad Max trigger inspection self-test")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("file", type=Path, nargs="?", help="Raw, unpacked YM2 file")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        if args.file is None:
            return
    if args.file is None:
        parser.error("provide a raw YM2 file or --self-test")
    frames, events = inspect(args.file.read_bytes())
    active = [e for e in events if e[2]]
    print(f"YM2: {frames} frames, {frames/50:.2f} s at 50 Hz")
    print(f"R10 bit7 trigger flags: {len(events)}")
    print(f"Triggers with nonzero R12: {len(active)}")
    print(f"Sample IDs: {dict(sorted(Counter(e[1] for e in active).items()))}")
    for frame, sample, timer, hz in active[:40]:
        print(f"  {frame/50:7.2f}s  sample={sample:3d}  R12={timer:3d}  rate={hz:.2f} Hz")
    if len(active) > 40:
        print(f"  ... and {len(active)-40} more triggers")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError) as exc:
        raise SystemExit(f"YM2 probe error: {exc}")
