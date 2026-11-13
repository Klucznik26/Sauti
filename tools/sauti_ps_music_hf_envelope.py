#!/usr/bin/env python3
"""SBR HF temporal-energy audit: compare short-time band powers against FFmpeg.

A different valid noise sequence can inflate sample-by-sample RMSE.
Here we measure the absolute dB deviation of band energy in 40-ms
windows and the correlation of the time-varying band-energy envelope.
Diagnostic only: not a normative HE-AAC compliance or perceptual test.
"""
import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

MODES = ("full", "limited", "band_limited", "energy_balanced"," "tonal", "no_noise", "noise_25", "noise_50", "noise_75", "low_only")
BANDS = ((6000, 12000), (12000, 24000))
EPS = 1e-25


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("source", type=Path, help="HE-AAC v2 ADTS")
    ap.add_argument("--pcm-dir", type=Path, default=Path("/tmp/sauti_bogini"))
    ap.add_argument("--rate", type=int, default=48000)
    ap.add_argument("--lag", type=int, default=384,
                    help="reference PCM offset relative to native sample zero")
    ap.add_argument("--window-ms", type=int, default=40)
    ap.add_argument("--positions", type=float, nargs="+",
                    default=[10, 30, 60, 90, 120, 140])
    ap.add_argument("--span", type=float, default=1.0,
                    help="number of seconds analyzed at each position")
    a = ap.parse_args()
    if not a.source.is_file():
        ap.error("source does not exist")
    if a.rate <= 0 or a.window_ms <= 0 or a.span <= 0 or a.lag < 0:
        ap.error("invalid rate/lag/window/span")
    block = a.rate * a.window_ms // 1000
    if block < 128:
        ap.error("window too small")
    window = np.hanning(block).astype(np.float64)[:, None]
    freq = np.fft.rfftfreq(block, 1 / a.rate)
    bins = [(name, (freq >= lo) & (freq < min(hi, a.rate / 2)))
            for lo, hi, name in ((6000, 12000, "6-12kHz"),
                                 (12000, 24000, "12-24kHz"))]
    with tempfile.TemporaryDirectory(prefix="sauti_hf_env_") as td:
        ref_file = Path(td) / "reference.f32le"
        res = subprocess.run([
            "ffmpeg", "-hide_banner", "-nostdin", "-loglevel", "error",
            "-y", "-i", str(a.source), "-map", "0:a:0",
            "-ac", "2", "-ar", str(a.rate), "-c:a", "pcm_f32le",
            "-f", "f32le", str(ref_file)
        ], capture_output=True, text=True)
        if res.returncode:
            raise RuntimeError("Reference decode failed: " + res.stderr[-2000:])
        reference = np.memmap(ref_file, dtype="<f4", mode="r")
        if reference.size % 2:
            raise RuntimeError("reference is not stereo")
        reference = reference.reshape(-1, 2)

        def powers(pcm, start):
            frame = np.asarray(pcm[start:start + block], dtype=np.float64)
            if frame.shape != (block, 2) or not np.isfinite(frame).all():
                raise RuntimeError("invalid PCM window")
            fft = np.fft.rfft(frame * window, axis=0)
            return np.abs(fft) ** 2

        for mode in MODES:
            file = a.pcm_dir / (mode + ".f32le")
            if not file.is_file():
                print(f"skip {mode} (file absent)")
                continue
            pcm = np.memmap(file, dtype="<f4", mode="r")
            if pcm.size % 2:
                raise RuntimeError(f"{mode}: invalid stereo PCM")
            pcm = pcm.reshape(-1, 2)
            results = {name: [[], []] for name, _ in bins}
            for second in a.positions:
                start = round(second * a.rate)
                count = int(round(a.span * a.rate)) // block
                for j in range(count):
                    n = start + j * block
                    if n + block > len(pcm) or n + a.lag + block > len(reference):
                        continue
                    x = powers(pcm, n)
                    y = powers(reference, n + a.lag)
                    for name, region in bins:
                        if not region.any():
                            continue
                        px, py = float(x[region].sum()), float(y[region].sum())
                        if py <= EPS:
                            continue
                        results[name][0].append(px)
                        results[name][1].append(py)
            print(f"\\n=== {mode} ===")
            for name, (px, py) in results.items():
                if len(px) < 4:
                    print(f"  {name}: insufficient reference energy/windows")
                    continue
                x, y = np.asarray(px), np.asarray(py)
                delta = 10 * np.log10((x + EPS) / (y + EPS))
                lx, ly = np.log10(x + EPS), np.log10(y + EPS)
                corr = float(np.corrcoef(lx, ly)[0, 1]) if (
                    np.std(lx) > 1e-12 and np.std(ly) > 1e-12
                ) else float("nan")
                print(f"  {name}: windows={len(x)} median_bias={np.median(delta):+.2f}dB "
                      f"median_abs_error={np.median(np.abs(delta)):.2f}dB "
                      f"p90_abs_error={np.percentile(np.abs(delta), 90):.2f}dB "
                      f"energy_envelope_corr={corr:.5f}")
    print("\\nDiagnostics only: block energy envelopes; no sample-phase matching required.")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, ValueError) as exc:
        print("FAIL SBR temporal energy:", exc, file=sys.stderr)
        sys.exit(1)
