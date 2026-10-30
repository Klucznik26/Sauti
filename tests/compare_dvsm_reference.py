#!/usr/bin/env python3
"""Independent reference comparison for DVSM Delta/Voice, mono and stereo.

Decodes from format specification using Python only. PACK extension overrides are
not handled, and are deliberately rejected. Not an original WinRec binary.
"""
import argparse
import math
import struct
import sys
from pathlib import Path

VOICE = [-8192, -4096, -2048, -1024, -512, -256, -64, 0,
         64, 256, 512, 1024, 2048, 4096, 8192, 16384]


def clip(x):
    return max(-32768, min(32767, x))


def delta_table():
    values = [0] * 256
    for index in range(1, 128):
        values[index] = min(32767, math.floor(1.084618362 ** index + 0.5))
    values[128] = -32768
    for index in range(129, 256):
        values[index] = -min(32767, math.floor(1.084618362 ** (256-index) + 0.5))
    return values


def reference(blob):
    if len(blob) < 18 or blob[:6] != b"DVSM\0\0":
        raise ValueError("Invalid DVSM signature or short input")
    offset, freq = struct.unpack_from(">HH", blob, 6)
    pack, mode = blob[10:12]
    block = struct.unpack_from(">I", blob, 12)[0]
    if offset != 16:
        raise ValueError("Header extensions not audited, especially PACK")
    if freq in range(8, 257):
        raise ValueError("Reserved frequency index")
    if pack not in (2, 4) or mode not in (1, 3):
        raise ValueError("Only packed Delta/Voice 16-bit mono/stereo is audited")
    channels = 1 if mode & 2 else 2
    table = delta_table() if pack == 2 else VOICE
    data = blob[offset:]
    pcm = []
    if channels == 1:
        if len(data) < 2:
            raise ValueError("Mono seed missing")
        predictor = struct.unpack_from(">h", data)[0]
        pcm.append(predictor)
        for byte in data[2:]:
            indices = [byte] if pack == 2 else [byte >> 4, byte & 15]
            for index in indices:
                predictor = clip(predictor + table[index])
                pcm.append(predictor)
    else:
        if block < 4 or (pack == 2 and block % 2):
            raise ValueError("Invalid stereo block length")
        cursor = 0
        while cursor < len(data):
            part = data[cursor:cursor+block]
            if len(part) < 4 or (pack == 2 and len(part) % 2):
                raise ValueError("Truncated stereo block")
            left, right = struct.unpack_from(">hh", part)
            pcm.extend((left, right))
            if pack == 2:
                for index in range(4, len(part), 2):
                    left = clip(left + table[part[index]])
                    right = clip(right + table[part[index+1]])
                    pcm.extend((left, right))
            else:
                for byte in part[4:]:
                    left = clip(left + table[byte >> 4])
                    right = clip(right + table[byte & 15])
                    pcm.extend((left, right))
            cursor += len(part)
    return channels, pcm


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dvs", type=Path)
    parser.add_argument("sauti_raw", type=Path)
    args = parser.parse_args()
    blob = args.dvs.read_bytes()
    raw = args.sauti_raw.read_bytes()
    if len(raw) % 2:
        raise ValueError("Odd PCM output byte count")
    channels, reference_pcm = reference(blob)
    samples = len(raw) // 2
    print(f"DVSM bytes={len(blob)} channels={channels} "
          f"reference_frames={len(reference_pcm)//channels} "
          f"sauti_frames={samples//channels}")
    if samples != len(reference_pcm):
        print("FAIL: PCM sample count differs")
        return 1
    mismatches = 0
    for index, expected in enumerate(reference_pcm):
        actual = struct.unpack_from("<h", raw, index * 2)[0]
        if actual != expected:
            if mismatches < 12:
                print(f"MISMATCH frame={index//channels} channel={index%channels}: "
                      f"reference={expected} sauti={actual}")
            mismatches += 1
    if mismatches:
        print(f"FAIL: {mismatches} differing samples")
        return 1
    print(f"PASS: all {samples} samples bit-identical to independent Python reference")
    print("Not a comparison against original WinRec software or independent vintage converter.")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (ValueError, OSError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        sys.exit(2)
