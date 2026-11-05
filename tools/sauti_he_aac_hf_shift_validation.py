#!/usr/bin/env python3
"""SBR-54p: out-of-sample check of an apparent HF timing offset.

SBR-54o found a maximum at +288 samples, but searching many shifts
can overfit. Here we select the shift on one HALF of a recording and
measure the same shift on a separate held-out HALF. Then swap halves.

LOW vs FFmpeg stays FIXED at the previously determined lag -6.
Only HF=(candidate-LOW proxy) is shifted, and a common interior
window excludes all non-overlapping samples. Every sample is read
without normalization, clipping, or external codecs.
"""
import argparse
import math
import random
import sys

from sauti_he_aac_fixed_lag_audit import align, load
from sauti_he_aac_hf_shift_audit import basis, score


def data(low, candidate, reference, lag, radius):
    a, c, r = align(low, candidate, reference, lag)
    if radius < 0 or len(a) - 2 * radius < 4096:
        raise ValueError("not enough common samples for half-frame validation")
    hf = [y - x for x, y in zip(a, c)]
    middle = radius + (len(a) - 2 * radius) // 2
    first = list(range(radius, middle))
    second = list(range(middle, len(a) - radius))
    if min(len(first), len(second)) < 2048:
        raise ValueError("a validation half is too short")
    return a, hf, r, first, second


def measure(low, hf, ref, positions, shift):
    b, residual, bb, rr = basis(low, ref, positions)
    return score(hf, positions, b, residual, bb, rr, shift)


def select_shift(low, hf, ref, positions, maximum):
    # A sample-exact coarse sweep with a constant train window.
    sample = positions[::16]
    b, residual, bb, rr = basis(low, ref, sample)
    ranked = []
    for shift in range(-maximum, maximum + 1):
        c = score(hf, sample, b, residual, bb, rr, shift)
        ranked.append((abs(c), shift))
    ranked.sort(reverse=True)
    finalists = {0}
    for _, center in ranked[:16]:
        for candidate in range(max(-maximum, center - 2),
                               min(maximum, center + 2) + 1):
            finalists.add(candidate)
    b, residual, bb, rr = basis(low, ref, positions)
    scored = [(score(hf, positions, b, residual, bb, rr, shift),
               shift) for shift in sorted(finalists)]
    return max(scored, key=lambda row: abs(row[0]))


def evaluate(low, candidate, reference, lag=-6, maximum=512,
             candidate_shift=288):
    base, hf, ref, first, second = data(
        low, candidate, reference, lag, maximum)
    result = []
    for name, train, holdout in (
        ("first_half", first, second),
        ("second_half", second, first),
    ):
        train_corr, shift = select_shift(base, hf, ref, train, maximum)
        validation_corr = measure(base, hf, ref, holdout, shift)
        fixed_train = measure(base, hf, ref, train, candidate_shift)
        fixed_holdout = measure(base, hf, ref, holdout, candidate_shift)
        zero_train = measure(base, hf, ref, train, 0)
        zero_holdout = measure(base, hf, ref, holdout, 0)
        result.append({
            "train": name, "chosen_shift": shift,
            "train_corr": train_corr,
            "validation_corr": validation_corr,
            "fixed_train": fixed_train,
            "fixed_holdout": fixed_holdout,
            "zero_train": zero_train,
            "zero_holdout": zero_holdout,
            "train_samples": len(train),
            "validation_samples": len(holdout),
        })
    return result


def self_test():
    n = 8192
    rng = random.Random(542)
    low = [0.08 * math.sin(i * .03) + 0.04 * math.cos(i * .067)
           for i in range(n)]
    independent = [0.03 * (rng.random() - .5)
                   for _ in range(n)]
    target = [a + 0.6 * b for a, b in zip(low, independent)]
    reference = target[6:] + [0.0] * 6

    for shift in (37, -48):
        # The shifted HF is the only additional signal in candidate.
        candidate = [
            low[i] +
            (independent[i - shift] if 0 <= i - shift < n else 0.0)
            for i in range(n)
        ]
        result = evaluate(low, candidate, reference,
                          lag=-6, maximum=96, candidate_shift=shift)
        for row in result:
            assert row["chosen_shift"] == shift, row
            assert row["train_corr"] > 0.999, row
            assert row["validation_corr"] > 0.999, row
            assert row["fixed_holdout"] > 0.999, row
        print(f"SBR-54p: stable {shift:+d}-sample HF offset verified "
              "on disjoint held-out halves PASS")

    # An accidental whole-file optimum must not survive when the
    # independent residual source changes in the second half.
    new_noise = [0.03 * (rng.random() - .5)
                 for _ in range(n)]
    different_half = [
        low[i] +
        (independent[i - 37] if 0 <= i - 37 < n // 2 else
         new_noise[i + 24] if n // 2 <= i and i + 24 < n else 0.0)
        for i in range(n)
    ]
    changed = evaluate(low, different_half, reference,
                       lag=-6, maximum=96, candidate_shift=37)
    assert changed[0]["train_corr"] > 0.99, changed
    assert abs(changed[0]["validation_corr"]) < 0.2, changed
    print("SBR-54p: spurious good training correlation rejected "
          "by independent holdout PASS")
    print("PASS SBR-54p HF shift cross-validation "
          "(no QMF/PCM decoder changes)")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--lag", type=int, default=-6,
                        help="fixed LOW-to-FFmpeg lag (default -6)")
    parser.add_argument("--max-shift", type=int, default=512,
                        help="HF-only shift search radius (default 512)")
    parser.add_argument("--test-shift", type=int, default=288,
                        help="preselected SBR-54o HF shift (default +288)")
    parser.add_argument("low", nargs="?")
    parser.add_argument("candidate", nargs="?")
    parser.add_argument("reference", nargs="?")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    if not all((args.low,args.candidate,args.reference)):
        parser.error("LOW proxy, SBR-54m HF candidate and FFmpeg files "
                     "are required")
    if abs(args.test_shift) > args.max_shift:
        parser.error("abs(--test-shift) must be <= --max-shift")
    try:
        rows = evaluate(load(args.low),load(args.candidate),
                        load(args.reference),args.lag,
                        args.max_shift,args.test_shift)
    except (OSError, ValueError) as exc:
        print(f"SBR-54p validation error: {exc}",file=sys.stderr)
        return 2
    print(f"SBR-54p: LOW/FFmpeg lag FIXED {args.lag}; "
          f"HF-only scan +/-{args.max_shift}; "
          f"preselected HF shift {args.test_shift:+d} samples")
    for row in rows:
        print(f"  TRAIN {row['train']} ({row['train_samples']} samples): "
              f"best_shift={row['chosen_shift']:+d}, "
              f"train_corr={row['train_corr']:+.6f}, "
              f"heldout_corr={row['validation_corr']:+.6f}, "
              f"heldout_residual_explained="
              f"{row['validation_corr']**2:.3%}")
        print(f"    shift {args.test_shift:+d}: "
              f"train_corr={row['fixed_train']:+.6f}, "
              f"heldout_corr={row['fixed_holdout']:+.6f}; "
              f"shift 0 train={row['zero_train']:+.6f}, "
              f"heldout={row['zero_holdout']:+.6f}")
    print("Diagnostic cross-validation only: one good held-out correlation "
          "does NOT prove normative QMF delay; retained signed correlation "
          "is essential. NO PCM or decoder changes.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
