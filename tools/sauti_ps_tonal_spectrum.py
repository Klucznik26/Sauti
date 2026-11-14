#!/usr/bin/env python3
"""PS-5 tonal-spectrum diagnostic: narrow FFT peaks against FFmpeg.

Reference-derived spectral peaks are NOT necessarily SBR-signaled harmonics.
Measures local peak position, level, prominence and adjacent-window frequency
stability, not oscillator phase, ISO conformance or perceived audio quality.
Runs only on already generated experimental stereo f32le files.
"""
import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

EPS = 1e-24
MODES = ("energy_balanced", "tonal")


def spectrum(pcm, start, size, hann):
    frame = np.asarray(pcm[start:start + size], dtype=np.float64)
    if frame.shape != (size, 2) or not np.isfinite(frame).all():
        raise ValueError("non-finite or incomplete stereo PCM window")
    return np.mean(np.abs(np.fft.rfft(frame * hann[:, None], axis=0)) ** 2,
                   axis=1)


def select_peaks(ref, freqs, lo, hi, bin_hz, count, prominence_db):
    """Choose strong, separated local maxima from FFmpeg only."""
    radius = max(2, int(round(125.0 / bin_hz)))
    bounds = np.flatnonzero((freqs >= lo) & (freqs < hi))
    if len(bounds) < 2 * radius + 1:
        return []
    candidates = []
    for k in bounds:
        if k < radius or k + radius >= len(ref):
            continue
        if ref[k] <= ref[k - 1] or ref[k] < ref[k + 1]:
            continue
        neighborhood = ref[k - radius:k + radius + 1]
        floor = float(np.median(neighborhood))
        prominence = 10.0 * np.log10((float(ref[k]) + EPS) / (floor + EPS))
        if prominence >= prominence_db:
            candidates.append((int(k), prominence))
    candidates.sort(key=lambda pair: float(ref[pair[0]]), reverse=True)
    selected = []
    separation = max(2, int(round(150.0 / bin_hz)))
    for k, prominence in candidates:
        if all(abs(k - old[0]) >= separation for old in selected):
            selected.append((k, prominence))
        if len(selected) >= count:
            break
    return selected


def measure_peak(spec, k, radius, context):
    left, right = max(1, k - radius), min(len(spec) - 1, k + radius)
    peak = int(left + np.argmax(spec[left:right + 1]))
    bg_left, bg_right = max(1, k - context), min(len(spec) - 1, k + context)
    floor = float(np.median(spec[bg_left:bg_right + 1]))
    prominence = 10.0 * np.log10((float(spec[peak]) + EPS) / (floor + EPS))
    return peak, float(spec[peak]), prominence


def compare_window(native, reference, start, lag, size, hann, freqs,
                   lo, hi, max_peaks, min_prominence, tolerance):
    ref = spectrum(reference, start + lag, size, hann)
    got = spectrum(native, start, size, hann)
    bin_hz = float(freqs[1] - freqs[0])
    anchors = select_peaks(ref, freqs, lo, hi, bin_hz,
                           max_peaks, min_prominence)
    radius = max(1, int(round(tolerance / bin_hz)))
    context = max(radius + 2, int(round(125.0 / bin_hz)))
    rows = []
    for k, ref_prom in anchors:
        cand_k, cand_power, cand_prom = measure_peak(got, k, radius, context)
        rows.append((float(freqs[k]), float(freqs[cand_k] - freqs[k]),
                     10.0 * np.log10((cand_power + EPS) / (float(ref[k]) + EPS)),
                     cand_prom - ref_prom, cand_prom >= min_prominence))
    return rows


def report(mode, entries, pairs):
    if not entries:
        print(f"{mode}: no qualifying reference tonal peaks/windows")
        return
    vals = np.asarray(entries, dtype=float)
    displacement = np.abs(vals[:, 1])
    amplitude = np.abs(vals[:, 2])
    contrast = np.abs(vals[:, 3])
    print(f"{mode}: peaks={len(vals)} "
          f"median_freq_error={np.median(displacement):.1f}Hz "
          f"p90_freq_error={np.percentile(displacement, 90):.1f}Hz "
          f"median_abs_level_error={np.median(amplitude):.2f}dB "
          f"median_abs_prominence_error={np.median(contrast):.2f}dB "
          f"prominence_hit_rate={np.mean(vals[:, 4]):.3f} "
          f"adjacent_peak_drift={np.median(pairs):.1f}Hz"
          if pairs else
          f"{mode}: peaks={len(vals)} "
          f"median_freq_error={np.median(displacement):.1f}Hz "
          f"p90_freq_error={np.percentile(displacement, 90):.1f}Hz "
          f"median_abs_level_error={np.median(amplitude):.2f}dB "
          f"median_abs_prominence_error={np.median(contrast):.2f}dB "
          f"prominence_hit_rate={np.mean(vals[:, 4]):.3f} "
          "adjacent_peak_drift=NA")


def self_test():
    rate, size = 48000, 3840
    time = np.arange(size, dtype=np.float64) / rate
    tone = (0.7 * np.sin(2 * np.pi * 10250 * time) +
            0.2 * np.sin(2 * np.pi * 14375 * time))
    ref = np.repeat(tone[:, None], 2, axis=1)
    offset = np.repeat((0.7 * np.sin(2 * np.pi * 10500 * time) +
                        0.2 * np.sin(2 * np.pi * 14375 * time))[:, None], 2, axis=1)
    hann = np.hanning(size)
    freqs = np.fft.rfftfreq(size, 1 / rate)
    exact = compare_window(ref, ref, 0, 0, size, hann, freqs,
                           6000, 24000, 6, 6, 300)
    shifted = compare_window(offset, ref, 0, 0, size, hann, freqs,
                             6000, 24000, 6, 6, 300)
    assert len(exact) >= 2 and len(shifted) >= 2
    assert max(abs(row[1]) for row in exact) < 0.01
    assert max(abs(row[2]) for row in exact) < 1e-6
    assert max(abs(row[1]) for row in shifted) >= 200
    print("PASS PS tonal-spectrum synthetic peaks: identity, shifted frequency, level")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("source", type=Path, nargs="?", help="HE-AACv2 ADTS")
    ap.add_argument("--pcm-dir", type=Path, default=Path("/tmp/sauti_bogini"))
    ap.add_argument("--rate", type=int, default=48000)
    ap.add_argument("--lag", type=int, default=384)
    ap.add_argument("--window-ms", type=int, default=80)
    ap.add_argument("--hop-ms", type=int, default=40)
    ap.add_argument("--positions", type=float, nargs="+",
                    default=[10, 30, 60, 90, 120, 140])
    ap.add_argument("--span", type=float, default=1.0)
    ap.add_argument("--min-prominence", type=float, default=8.0)
    ap.add_argument("--max-peaks", type=int, default=6)
    ap.add_argument("--tolerance-hz", type=float, default=75.0)
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args()
    if a.self_test:
        self_test()
        return
    if a.source is None or not a.source.is_file():
        ap.error("source HE-AACv2 ADTS required")
    if (a.rate <= 0 or a.lag < 0 or a.window_ms <= 0 or a.hop_ms <= 0
            or a.span <= 0 or a.max_peaks <= 0 or a.tolerance_hz <= 0
            or a.min_prominence < 0 or any(p < 0 for p in a.positions)):
        ap.error("invalid numeric arguments")
    size = round(a.rate * a.window_ms / 1000)
    hop = round(a.rate * a.hop_ms / 1000)
    if size < 256 or hop < 1:
        ap.error("window/hop too small")
    freqs = np.fft.rfftfreq(size, 1 / a.rate)
    hann = np.hanning(size)
    with tempfile.TemporaryDirectory(prefix="sauti_tonal_spectrum_") as td:
        path = Path(td) / "ffmpeg.f32le"
        cmd = ["ffmpeg", "-hide_banner", "-nostdin", "-loglevel", "error",
               "-y", "-i", str(a.source), "-map", "0:a:0", "-ac", "2",
               "-ar", str(a.rate), "-c:a", "pcm_f32le", "-f", "f32le", str(path)]
        done = subprocess.run(cmd, capture_output=True, text=True)
        if done.returncode:
            raise RuntimeError("FFmpeg decode failed: " + done.stderr[-2000:])
        ref = np.memmap(path, dtype="<f4", mode="r")
        if ref.size % 2:
            raise ValueError("FFmpeg PCM is not stereo")
        ref = ref.reshape(-1, 2)
        for mode in MODES:
            f = a.pcm_dir / (mode + ".f32le")
            if not f.is_file():
                raise FileNotFoundError(f"missing {f}; generate {mode} preview first")
            data = np.memmap(f, dtype="<f4", mode="r")
            if data.size % 2:
                raise ValueError(f"{mode}: PCM is not stereo")
            data = data.reshape(-1, 2)
            print(f"\n=== {mode} ===")
            for lo, hi in ((6000, 12000), (12000, min(a.rate / 2, 24000))):
                entries, drift = [], []
                for p in a.positions:
                    begin = round(p * a.rate)
                    previous = {}
                    steps = max(1, int((a.span * a.rate - size) // hop) + 1)
                    for j in range(steps):
                        start = begin + j * hop
                        if start + size > len(data) or start + a.lag + size > len(ref):
                            continue
                        rows = compare_window(data, ref, start, a.lag, size, hann,
                                              freqs, lo, hi, a.max_peaks,
                                              a.min_prominence, a.tolerance_hz)
                        entries.extend(rows)
                        current = {round(row[0] / 150):row[1] for row in rows}
                        for key, displacement in current.items():
                            if key in previous:
                                drift.append(abs(displacement - previous[key]))
                        previous = current
                print(f"  {lo/1000:g}-{hi/1000:g}kHz ", end="")
                report(mode, entries, drift)
    print("Diagnostic: FFmpeg reference-derived peaks, not proven SBR harmonic flags; "
          "no oscillator phase or perceptual score.")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, ValueError, AssertionError) as exc:
        print("FAIL PS tonal-spectrum:", exc, file=sys.stderr)
        sys.exit(1)
