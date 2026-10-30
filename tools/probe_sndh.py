#!/usr/bin/env python3
"""Inspect an Atari ST SNDH candidate without executing embedded 68000 code.

SNDH is usually after three initial 68000 branch instructions, not at byte 0.
A signature match is only a preliminary structural identification.
"""
import argparse
from pathlib import Path


def inspect(data):
    if len(data) < 16:
        raise ValueError("too short for SNDH")
    offset = data.find(b"SNDH", 0, min(len(data), 512))
    if offset < 0:
        raise ValueError("SNDH marker absent from the first 512 bytes")
    if offset < 6:
        raise ValueError("implausibly early SNDH marker")
    return offset


def self_test():
    assert inspect(bytes.fromhex("600000106000000c60000008") + b"SNDH" + b"\0"*12) == 12
    for invalid in (b"SNDH"+b"\0"*32, b"\0"*32, b"\0"*4+b"SNDH"):
        try:
            inspect(invalid)
        except ValueError:
            pass
        else:
            raise AssertionError("malformed candidate accepted")
    print("PASS: SNDH preliminary structural probe")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("file", type=Path, nargs="?")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()
    if args.self_test:
        self_test()
    if args.file:
        data = args.file.read_bytes()
        print(f"SNDH marker offset: {inspect(data)}; input bytes: {len(data)}")
        print("Only a preliminary signature check; music replay not verified.")
    elif not args.self_test:
        ap.error("provide an SNDH file or --self-test")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError) as exc:
        raise SystemExit(f"SNDH probe error: {exc}")
