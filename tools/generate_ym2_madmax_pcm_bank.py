#!/usr/bin/env python3
"""Generate user-owned synthetic unsigned PCM8 samples for YM2 Mad Max tests.

Not an emulation of the historical ST-Sound sample bank.
"""
import argparse
import math
from pathlib import Path

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("directory", type=Path)
    args = p.parse_args()
    args.directory.mkdir(parents=True, exist_ok=True)
    # Long enough to hear at MFP rates (51200..122880 samples/second).
    configurations = ((1, 330), (2, 495), (3, 660))
    for sample_id, pitch in configurations:
        length = 32768
        pcm = bytes(
            max(0, min(255, round(128 + 95 *
                math.sin(2 * math.pi * pitch * n / 80000) *
                (1 - n / length))))
            for n in range(length)
        )
        filename = args.directory / f"{sample_id}.raw"
        filename.write_bytes(pcm)
        print(f"{filename}: {len(pcm)} bytes, PCM8 unsigned")
    print("Set SAUTI_YM2_BANK_DIR to this directory before launching Sauti.")

if __name__ == "__main__":
    main()
