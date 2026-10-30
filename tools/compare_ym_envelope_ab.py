#!/usr/bin/env python3
"""Compare both isolated YM envelope render variants using identical PCM metrics.

Reads existing WAVs only; never modifies the player or the reference renders.
"""
import argparse
import math
from pathlib import Path

from compare_ym_pcm import read_pcm, metrics, gain_fit_metrics, envelope_rms, stsound_filter

SHAPES = ("09", "0a", "0c", "0e")


def analyze(candidate_path, reference_path):
    rate_a, candidate = read_pcm(candidate_path)
    rate_b, reference = read_pcm(reference_path)
    if rate_a != rate_b:
        raise ValueError(f"sample-rate mismatch: {candidate_path} / {reference_path}")
    # Match the existing --stsound-filter --candidate-gain 0.5 path.
    candidate = [value * 0.5 for value in stsound_filter(candidate)]
    count, error, _, correlation = metrics(candidate, reference)
    gain, residual = gain_fit_metrics(candidate, reference)
    contour_a = envelope_rms(candidate[:count], rate_a)
    contour_b = envelope_rms(reference[:count], rate_a)
    _, contour_error, _, contour_corr = metrics(contour_a, contour_b)
    return correlation, error, gain, residual, contour_corr, contour_error


def rows(root):
    for shape in SHAPES:
        name = f"envelope_{shape}"
        old = root / "reference-dac"
        new = root / "env-pre-tick"
        reference = old / f"{name}.stsound.wav"
        yield shape, analyze(old / f"{name}.sauti.wav", reference), analyze(
            new / f"{name}.sauti.wav", reference)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path("build/ym-reference"))
    args = parser.parse_args()
    print("YM envelopes: filtered Sauti x0.5 vs ST-Sound (zero lag; same reference WAV)")
    print("shape | corr old  pre-tick   delta | RMS old  pre-tick   delta | contour corr old  pre-tick")
    improved = 0
    for shape, old, new in rows(args.root):
        if new[1] < old[1]:
            improved += 1
        print(f"  {shape}  | {old[0]:.6f} {new[0]:.6f} {new[0]-old[0]:+.6f} | "
              f"{old[1]:.6f} {new[1]:.6f} {new[1]-old[1]:+.6f} | "
              f"{old[4]:.6f} {new[4]:.6f}")
    print(f"RMS improved in {improved}/{len(SHAPES)} shapes; "
          "positive corr delta and negative RMS delta are improvements.")
    print("Note: global zero-lag scores, no per-shape lag optimization; "
          "phase/periodic ambiguity remains.")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, EOFError) as exc:
        raise SystemExit(f"YM envelope A/B failed: {exc}")
