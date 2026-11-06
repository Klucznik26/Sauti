#!/usr/bin/env python3
"""SBR-67: fixed-sign A/B of SBR-54h experimental harmonic-preview delta.

Compare original PCM and an anti-delta thought experiment:
    delta = sine_preview - baseline_preview
    inverse = baseline_preview - delta
This flips ALL of the SBR-54h preview difference, including the noise
removal, NOT just the oscillator's sine. It is NOT a production fix and
does not identify a normative SBR phase without a complex QMF reference.
"""
import argparse
import math

from sauti_he_aac_compare_pcm import compare, load
from sauti_sbr66_tonal_ab_audit import FRAME, RATE, evaluate


def invert_delta(baseline, sine):
    if len(baseline) != len(sine) or not baseline or len(baseline) % FRAME:
        raise ValueError("baseline/sine must have identical complete PCM frames")
    if not all(math.isfinite(float(v)) for a in (baseline, sine) for v in a):
        raise ValueError("non-finite preview")
    # Fixed, coefficient-free A/B; no fitting to the reference allowed.
    inverse = [2.0 * float(b) - float(s)
               for b, s in zip(baseline, sine)]
    if not all(math.isfinite(v) for v in inverse):
        raise ValueError("inverted delta overflow")
    delta_rms = math.sqrt(math.fsum((s - b) ** 2
                                    for b, s in zip(baseline, sine)) /
                          len(baseline))
    if delta_rms <= 1e-12:
        raise ValueError("no measurable SBR-54h A/B delta")
    return inverse, delta_rms


def self_test():
    count = 8 * FRAME
    low = [0.2 * math.sin(2.0 * math.pi * 750.0 * i / RATE)
           for i in range(count)]
    tonal = [
        0.006 * math.sin(2.0 * math.pi * 6937.5 * i / RATE)
        + 0.004 * math.sin(2.0 * math.pi * 15937.5 * i / RATE)
        for i in range(count)
    ]
    baseline = low[:]
    sine = [x - 0.65 * t for x, t in zip(low, tonal)]
    lag, gain, bias = 384, 1.414, 0.001
    reference = [0.0] * lag + [
        gain * (x + t) + bias for x, t in zip(low, tonal)
    ] + [0.0] * FRAME
    inverted, drms = invert_delta(baseline, sine)
    assert drms > 0.0
    for x, y, z in zip(baseline, sine, inverted):
        assert abs((z - x) + (y - x)) < 1e-12
    rows = evaluate(low, reference,
                    {"baseline": baseline,
                     "sine": sine,
                     "inverse_delta": inverted},
                    lag, gain, bias, 2, 6)["variants"]
    for k in range(2):
        bb = rows["baseline"]["peaks"][k]
        ss = rows["sine"]["peaks"][k]
        inv = rows["inverse_delta"]["peaks"][k]
        assert bb["relative_error"] > 0.99
        assert ss["signed"] < -0.999 and ss["relative_error"] > 1.6
        assert inv["signed"] > 0.999 and inv["relative_error"] < 0.36
    for left, right in ((baseline[:-1], sine), (baseline, sine[:-1])):
        try:
            invert_delta(left, right)
        except ValueError:
            pass
        else:
            raise AssertionError("mismatched/partial PCM accepted")
    try:
        invert_delta(low, low)
    except ValueError:
        pass
    else:
        raise AssertionError("zero-delta was accepted")
    contaminated = sine[:]
    contaminated[100] = float("nan")
    try:
        invert_delta(baseline, contaminated)
    except ValueError:
        pass
    else:
        raise AssertionError("NaN accepted")
    print("PASS SBR-67: inverted preview delta, opposite-phase tones, geometry and NaN")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--self-test", action="store_true")
    p.add_argument("--low")
    p.add_argument("--reference")
    p.add_argument("--baseline")
    p.add_argument("--sine")
    p.add_argument("--first-frame", type=int, default=3)
    p.add_argument("--last-frame", type=int, default=46)
    args = p.parse_args()
    if args.self_test:
        self_test()
        return 0
    if not all((args.low, args.reference, args.baseline, args.sine)):
        p.error("--low, --reference, --baseline and --sine are required")
    try:
        low = load(args.low)
        reference = load(args.reference)
        baseline = load(args.baseline)
        sine = load(args.sine)
        if not low or len(low) != len(baseline):
            raise ValueError("LOW and preview must have equal length")
        lag, corr, _, gain, bias, _, _ = compare(low, reference)
        if not math.isfinite(corr) or corr < 0.98 or not math.isfinite(gain):
            raise ValueError("LOW alignment too weak")
        inverted, drms = invert_delta(baseline, sine)
        result = evaluate(low, reference,
                          {"baseline": baseline, "sine": sine,
                           "inverse_delta": inverted},
                          lag, gain, bias, args.first_frame, args.last_frame)
    except (OSError, ValueError) as e:
        p.exit(2, f"SBR-67 refused: {e}\n")
    print(f"SBR-67 DIAGNOSTIC: frames={result['frames']}; "
          f"LOW lag={lag}, corr={corr:.8f}, gain={gain:.9g}; "
          f"preview delta RMS={drms:.9g}")
    baseline = result["variants"]["baseline"]
    for name, row in result["variants"].items():
        print(f"[{name}]")
        for idx, peak in enumerate(row["peaks"]):
            prior = baseline["peaks"][idx]["relative_error"]
            new = peak["relative_error"]
            def fmt(v):
                return "undefined" if v is None else f"{v:.6f}"
            change = prior - new if prior is not None and new is not None else None
            print(f"  k={peak['band']:02d} f={peak['hz']:.1f}Hz "
                  f"amplitude={fmt(peak['amplitude'])} "
                  f"coherence={fmt(peak['signed'])} "
                  f"relative_error={fmt(new)} improvement={fmt(change)}")
        for band in row["bands"]:
            if band["start"] == 8625 or band["start"] == 12000:
                continue
            print(f"  {band['start']}-{band['end']}Hz "
                  f"corr={fmt(band['signed'])} "
                  f"error={fmt(band['relative_error'])}")
    print("NON-NORMATIVE thought experiment: inverted entire SBR-54h delta "
          "(tone PLUS noise gating), not the oscillator alone. "
          "Do not deploy sign reversal based on PCM A/B.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
