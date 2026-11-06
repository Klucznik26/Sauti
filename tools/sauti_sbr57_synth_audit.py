#!/usr/bin/env python3
"""SBR-57: compare equal-length Sauti and FFmpeg QMF-synthesis PCM traces."""
import argparse
import array
import math
import sys
from pathlib import Path


def read_f32(path):
    raw = Path(path).read_bytes()
    if not raw or len(raw) % 4:
        raise ValueError(f"{path}: expected nonempty f32le")
    values = array.array("f")
    values.frombytes(raw)
    if sys.byteorder != "little":
        values.byteswap()
    if not all(math.isfinite(value) for value in values):
        raise ValueError(f"{path}: nonfinite float")
    return values


def measure(sauti, ffmpeg):
    if len(sauti) != len(ffmpeg) or not sauti:
        raise ValueError("PCM length mismatch")
    reference_energy = math.fsum(value * value for value in ffmpeg)
    source_energy = math.fsum(value * value for value in sauti)
    if reference_energy <= 0.0 or source_energy <= 0.0:
        raise ValueError("zero PCM energy")
    gain = math.fsum(left * right for left, right in zip(sauti, ffmpeg)) / reference_energy
    residual = math.sqrt(math.fsum((left - gain * right) ** 2 for left, right in zip(sauti, ffmpeg)) / source_energy)
    maximum = max(abs(left - gain * right) for left, right in zip(sauti, ffmpeg))
    return gain, residual, maximum


def self_test():
    ffmpeg = [float(index + 1) for index in range(16)]
    sauti = [0.5 * value for value in ffmpeg]
    gain, residual, maximum = measure(sauti, ffmpeg)
    assert gain == 0.5 and residual == 0.0 and maximum == 0.0
    try:
        measure(sauti, ffmpeg[:-1])
    except ValueError:
        pass
    else:
        raise AssertionError("mismatched PCM accepted")
    print("PASS SBR-57 synthesis audit: scale, residual, geometry")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sauti_pcm")
    parser.add_argument("ffmpeg_pcm")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    try:
        gain, residual, maximum = measure(read_f32(args.sauti_pcm), read_f32(args.ffmpeg_pcm))
    except (OSError, ValueError) as exc:
        parser.exit(2, f"SBR-57 synthesis audit rejected: {exc}\n")
    print("SBR-57 QMF synthesis audit: diagnostic only; no decoder correction")
    print(f"gain(FFmpeg->Sauti)={gain:.9g} residual={residual:.9g} max_abs_after_gain={maximum:.9g}")


if __name__ == "__main__":
    main()
