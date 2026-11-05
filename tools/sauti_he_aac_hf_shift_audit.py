#!/usr/bin/env python3
"""SBR-54o: test whether HF is useful after shifting its own timing.

Keep the LOW/FFmpeg alignment FIXED (default -6 samples).
Search only the independently generated HF = candidate - LOW proxy
against the FFmpeg residual after removing fitted LOW+DC.
Identical interior samples are used for EVERY shift, so scores are
directly comparable without changing overlap. Diagnostic only.
"""
import argparse
import math
import random
import sys

from sauti_he_aac_fixed_lag_audit import align, load


def rows(baseline, candidate, reference, reference_lag, maximum):
    base, cand, ref = align(baseline, candidate, reference, reference_lag)
    n = len(base)
    if maximum < 0 or n <= 2 * maximum + 2048:
        raise ValueError("max HF shift leaves fewer than 2048 common samples")
    delta = [c - b for c, b in zip(cand, base)]
    # The guard is symmetric, making the interior window IDENTICAL
    # for positive/negative HF shifts. LOW and reference are not shifted.
    return base, delta, ref, list(range(maximum, n - maximum))


def basis(base, ref, positions):
    """Residual from the best LOW+DC fit on this particular stride."""
    n = len(positions)
    mean_b = sum(base[i] for i in positions) / n
    mean_r = sum(ref[i] for i in positions) / n
    bb = sum((base[i] - mean_b) ** 2 for i in positions)
    if bb <= 0.0:
        raise ValueError("LOW contains no variable data in test range")
    br = sum((base[i] - mean_b) * (ref[i] - mean_r)
             for i in positions)
    coefficient = br / bb
    residual = [ref[i] - mean_r - coefficient * (base[i] - mean_b)
                for i in positions]
    residual_power = sum(x*x for x in residual)
    if residual_power <= 0.0:
        raise ValueError("FFmpeg has no LOW-fit residual to explain")
    centered_b = [base[i] - mean_b for i in positions]
    return centered_b, residual, bb, residual_power


def score(delta, positions, low_centered, residual, bb, rr, shift):
    n = len(positions)
    summation = squared = low_dot = residual_dot = 0.0
    for pos, b, r in zip(positions, low_centered, residual):
        d = delta[pos + shift]
        summation += d
        squared += d*d
        low_dot += d*b
        residual_dot += d*r
    # Subtract the LOW projection and DC from the shifted HF.
    orthogonal = squared - (summation*summation)/n -                  (low_dot*low_dot)/bb
    if orthogonal <= max(1e-21, squared*1e-13):
        return 0.0
    c = residual_dot / math.sqrt(orthogonal*rr)
    return max(-1.0, min(1.0, c))


def run_scan(baseline, candidate, reference, lag, maximum):
    base, hf, ref, full_positions = rows(
        baseline, candidate, reference, lag, maximum)
    # Every shift is visited at least once (no grid gaps), but the
    # first pass uses fewer time samples for bounded runtime.
    coarse_positions = full_positions[::16]
    c_b, c_r, c_bb, c_rr = basis(base, ref, coarse_positions)
    coarse = []
    for shift in range(-maximum, maximum + 1):
        correlation = score(hf, coarse_positions,
                            c_b, c_r, c_bb, c_rr, shift)
        coarse.append((abs(correlation), shift))
    coarse.sort(reverse=True)

    # Rescore promising sample-exact shifts with all samples.
    precise_positions = full_positions
    b, r, bb, rr = basis(base, ref, precise_positions)
    finalists = {0}
    for _, central in coarse[:12]:
        for offset in range(-2, 3):
            shift = central + offset
            if -maximum <= shift <= maximum:
                finalists.add(shift)
    evaluated = [
        (abs(score(hf, precise_positions,b,r,bb,rr,s)),
         s,score(hf, precise_positions,b,r,bb,rr,s))
        for s in sorted(finalists)
    ]
    best = max(evaluated)
    zero = next(x for x in evaluated if x[1] == 0)
    return {
        "lag": lag, "maximum": maximum,
        "n": len(full_positions), "best_shift": best[1],
        "best_corr": best[2], "zero_corr": zero[2],
        "best_explained": best[2]*best[2],
        "zero_explained": zero[2]*zero[2],
    }


def self_test():
    n = 8192
    rng = random.Random(540)
    low = [0.08*math.sin(i*0.022) + 0.045*math.cos(i*0.061)
           for i in range(n)]
    true_high = [0.035*(rng.random()-0.5) for _ in range(n)]
    for known_shift in (37, -48):
        cand = [
            low[i] + (true_high[i-known_shift]
                      if 0 <= i-known_shift < n else 0.0)
            for i in range(n)
        ]
        target = [low[i] + 0.7*true_high[i] for i in range(n)]
        # ref[i-6] = target[i]
        ref = target[6:] + [0.0]*6
        report = run_scan(low,cand,ref,-6,96)
        assert report["best_shift"] == known_shift, report
        assert report["best_explained"] > 0.999, report
        assert report["zero_explained"] < 0.01, report
        print("SBR-54o: isolated HF time shift",
              known_shift, "samples recovered PASS")
    print("PASS SBR-54o fixed-LOW-clock HF shift diagnostic self-test")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--lag", type=int, default=-6)
    parser.add_argument("--max-shift", type=int, default=512,
                        help="Only shift HF, up to +/- this many samples "
                             "(default 512); same reference window for all")
    parser.add_argument("low", nargs="?")
    parser.add_argument("candidate", nargs="?")
    parser.add_argument("reference", nargs="?")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    if not all((args.low,args.candidate,args.reference)):
        parser.error("LOW-proxy, candidate and FFmpeg paths are required")
    try:
        result = run_scan(load(args.low),load(args.candidate),
                          load(args.reference),args.lag,args.max_shift)
    except (OSError,ValueError) as exc:
        print(f"SBR-54o error: {exc}",file=sys.stderr)
        return 2
    print(f"SBR-54o: LOW/FFmpeg FIXED lag={result['lag']}, "
          f"HF-only search +/-{result['maximum']} samples, "
          f"identical overlap={result['n']} samples")
    print(f"  HF shift=0: residual corr={result['zero_corr']:.6f}, "
          f"residual explained={result['zero_explained']:.3%}")
    print(f"  best HF shift={result['best_shift']} samples, "
          f"residual corr={result['best_corr']:.6f}, "
          f"residual explained={result['best_explained']:.3%}")
    print("SBR-54o: best-of-many shift is an OPTIMISTIC diagnostic; "
          "not evidence of normative QMF timing or audio conformance. "
          "No PCM, decoder or QMF state modified.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
