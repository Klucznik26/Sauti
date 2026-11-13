#!/usr/bin/env python3
"""Compare Sauti HE-AAC v2 HF quality over several music windows, not just one second.
Diagnostic only. Requires numpy and FFmpeg; decodes the reference once.
"""
import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

BANDS = ((0, 2000), (2000, 6000), (6000, 12000), (12000, 24000))
MODES = ("full", "limited", "band_limited", "energy_balanced", "no_noise", "low_only", "noise_25", "noise_50", "noise_75")
EPS = 1e-24


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("source", type=Path, help="real HE-AAC v2 ADTS fixture")
    p.add_argument("--pcm-dir", type=Path, default=Path("/tmp/sauti_bogini"))
    p.add_argument("--rate", type=int, default=48000)
    p.add_argument("--lag", type=int, default=384,
                   help="positive reference sample offset for native[0]; see isolate output")
    p.add_argument("--seconds", type=int, default=1, help="FFT window length")
    p.add_argument("--positions", type=float, nargs="+",
                   default=[10, 30, 60, 90, 120, 140],
                   help="window starts in seconds of native PCM")
    args = p.parse_args()
    if args.rate <= 0 or args.seconds <= 0 or args.lag < 0:
        p.error("rate/seconds must be positive and lag nonnegative")
    if not args.source.is_file():
        p.error("HE-AAC input not found")
    block = args.rate * args.seconds
    with tempfile.TemporaryDirectory(prefix="sauti_hf_windows_") as td:
        outfile = Path(td) / "reference.f32le"
        command = ["ffmpeg", "-hide_banner", "-nostdin", "-loglevel", "error",
                   "-y", "-i", str(args.source), "-map", "0:a:0", "-ac", "2",
                   "-ar", str(args.rate), "-c:a", "pcm_f32le", "-f", "f32le",
                   str(outfile)]
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError("FFmpeg reference decode failed: " + result.stderr[-2000:])
        reference = np.memmap(outfile, dtype="<f4", mode="r")
        if reference.size % 2:
            raise RuntimeError("Reference PCM is not stereo")
        reference = reference.reshape(-1, 2)
        window = np.hanning(block).astype(np.float64)[:, None]
        freqs = np.fft.rfftfreq(block, 1 / args.rate)
        regions = [(lo, hi, (freqs >= lo) & (freqs < hi))
                   for lo, hi in BANDS if lo < args.rate / 2]
        for mode in MODES:
            path = args.pcm_dir / (mode + ".f32le")
            if not path.is_file():
                if mode.startswith("noise_"):
                    print(f"Skipping optional {mode}: {path} absent")
                    continue
                raise RuntimeError(f"Missing {path}; run tools/sauti_ps_isolate.py first")
            native = np.memmap(path, dtype="<f4", mode="r")
            if native.size % 2:
                raise RuntimeError(f"Nonstereo {path}")
            native = native.reshape(-1, 2)
            print(f"\n=== {mode} ===")
            scores = [[] for _ in regions]
            for second in args.positions:
                start = round(second * args.rate)
                if start < 0 or start + block > len(native) or start + args.lag + block > len(reference):
                    print(f"  {second:g}s: outside available PCM, skipped")
                    continue
                x = np.asarray(native[start:start + block], dtype=np.float64)
                y = np.asarray(reference[start + args.lag:start + args.lag + block],
                               dtype=np.float64)
                if not np.isfinite(x).all() or not np.isfinite(y).all():
                    raise RuntimeError(f"Nonfinite PCM at {second:g}s in {mode}")
                xx = np.abs(np.fft.rfft(x * window, axis=0)) ** 2
                yy = np.abs(np.fft.rfft(y * window, axis=0)) ** 2
                ee = np.abs(np.fft.rfft((x - y) * window, axis=0)) ** 2
                for idx, (lo, hi, bins) in enumerate(regions):
                    actual = yy[bins].sum()
                    if actual < EPS:
                        continue
                    ratio = 10 * np.log10((xx[bins].sum() + EPS) / (actual + EPS))
                    err = 10 * np.log10((ee[bins].sum() + EPS) / (actual + EPS))
                    scores[idx].append((second, ratio, err))
            for idx, (lo, hi, _) in enumerate(regions):
                if not scores[idx]:
                    print(f"  {lo//1000}-{hi//1000}kHz: no usable windows")
                    continue
                points = scores[idx]
                median_ratio = float(np.median([r for _, r, _ in points]))
                median_err = float(np.median([e for _, _, e in points]))
                worst = max(points, key=lambda x: x[2])
                print(f"  {lo//1000}-{hi//1000}kHz: median level {median_ratio:+.1f} dB, "
                      f"median error {median_err:+.1f} dB, "
                      f"worst error {worst[2]:+.1f} dB at {worst[0]:g}s "
                      f"({len(points)} windows)")
    print("\nDiagnostic only: relative spectral power and error against independently decoded FFmpeg; "
          "not a psychoacoustic quality score or ISO conformance check.")


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, OSError, ValueError) as error:
        print("FAIL HF music windows:", error, file=sys.stderr)
        sys.exit(1)
