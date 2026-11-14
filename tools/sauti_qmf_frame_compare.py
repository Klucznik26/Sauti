#!/usr/bin/env python3
"""Compare full 40-slot PCM from native AVX2 dispatch and forced-SSE2 QMF.

This is a per-sample regression gate, NOT an audio-quality or codec-completeness
claim. Input files are raw native little-endian IEEE-754 float32 PCM.
"""
import math
import struct
import sys
from pathlib import Path


def read_pcm(path: str) -> tuple[float, ...]:
    raw = Path(path).read_bytes()
    frame_bytes = 40 * 64 * 4
    if len(raw) < frame_bytes or len(raw) % frame_bytes:
        raise ValueError(f"{path}: expected a nonempty whole number of "
                         f"{frame_bytes}-byte frames; got {len(raw)} bytes")
    return struct.unpack(f"<{len(raw)//4}f", raw)


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: sauti_qmf_frame_compare.py auto.f32le sse2.f32le",
              file=sys.stderr)
        return 2
    try:
        auto = read_pcm(sys.argv[1])
        sse = read_pcm(sys.argv[2])
    except (ValueError, OSError, struct.error) as exc:
        print(f"FAIL QMF PCM input: {exc}", file=sys.stderr)
        return 1
    if len(auto) != len(sse):
        print(f"FAIL QMF PCM lengths differ: {len(auto)} vs {len(sse)} samples",
              file=sys.stderr)
        return 1
    max_abs = 0.0
    worst = 0
    sum_squared = 0.0
    for i, (a, b) in enumerate(zip(auto, sse)):
        if not math.isfinite(a) or not math.isfinite(b):
            print(f"FAIL QMF frame: nonfinite sample {i}", file=sys.stderr)
            return 1
        err = abs(a - b)
        scale = max(1.0, abs(a), abs(b))
        if err > max_abs:
            max_abs, worst = err, i
        sum_squared += err * err
        if err > 3e-6 * scale:
            print(f"FAIL QMF frame sample {i}: auto={a}, SSE2={b}, "
                  f"abs_err={err:.7g}, limit={3e-6 * scale:.7g}",
                  file=sys.stderr)
            return 1
    rms = math.sqrt(sum_squared / len(auto))
    print(f"PASS QMF PCM parity: {len(auto)//2560} frames, "
          f"{len(auto)} samples, "
          f"RMS error {rms:.7g}, max absolute error {max_abs:.7g} "
          f"at sample {worst}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
