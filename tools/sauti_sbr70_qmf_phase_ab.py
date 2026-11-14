#!/usr/bin/env python3
"""SBR-70: compare real independent Sauti phase0/phase+2 QMF PCM previews.

Unlike the SBR-67/68 PCM inversion thought experiments, both tested
candidates must come from independent Sauti complex QMF + 64-QMF synthesis.
The FFmpeg-minus-LOW residual still includes noise and LOW-model errors.
This tool never chooses an HF-specific scale, lag, phase, or frequency.
"""
import argparse
import math

from sauti_he_aac_compare_pcm import compare, load
from sauti_sbr66_tonal_ab_audit import FRAME, RATE, evaluate


def check_paths_and_streams(low, baseline, phase0, phase2):
    if not low or len(low) % FRAME:
        raise ValueError("LOW must have complete nonempty 2048-sample frames")
    if any(len(x) != len(low) for x in (baseline, phase0, phase2)):
        raise ValueError("phase0/phase2/baseline require equal full-stream length")
    if not all(math.isfinite(float(x)) for signal in
               (low, baseline, phase0, phase2) for x in signal):
        raise ValueError("nonfinite PCM")
    delta0 = math.fsum((float(s) - float(b)) ** 2
                       for s, b in zip(phase0, baseline))
    delta2 = math.fsum((float(s) - float(b)) ** 2
                       for s, b in zip(phase2, baseline))
    diff = math.fsum((float(a) - float(b)) ** 2
                     for a, b in zip(phase0, phase2))
    if not all(math.isfinite(x) for x in (delta0, delta2, diff)):
        raise ValueError("nonfinite PCM differences")
    if delta0 <= 1e-22 or diff <= 1e-22:
        raise ValueError("no measurable phase0/phase2 tonal A/B signal")
    return dict(delta0_rms=math.sqrt(delta0 / len(low)),
                delta2_rms=math.sqrt(delta2 / len(low)),
                phase_difference_rms=math.sqrt(diff / len(low)))


def self_test():
    count = 8 * FRAME
    low = [0.2 * math.sin(2 * math.pi * 650 * i / RATE)
           for i in range(count)]
    tone = [0.008 * math.sin(2 * math.pi * 6937.5 * i / RATE) +
            0.005 * math.sin(2 * math.pi * 15937.5 * i / RATE)
            for i in range(count)]
    # These two previews represent independent paths in the real test;
    # synthetic controls are deliberately opposite-phase at two centers.
    baseline = low[:]
    phase0 = [l - 0.7 * t for l, t in zip(low, tone)]
    phase2 = [l + 0.7 * t for l, t in zip(low, tone)]
    lag, gain, bias = 384, 1.414, 0.001
    reference = [0.0] * lag + [
        gain * (l + t) + bias for l, t in zip(low, tone)
    ] + [0.0] * FRAME
    stats = check_paths_and_streams(low, baseline, phase0, phase2)
    assert stats["phase_difference_rms"] > stats["delta0_rms"]
    rows = evaluate(low, reference,
                    {"baseline": baseline, "phase0": phase0, "phase2": phase2},
                    lag, gain, bias, 2, 6)["variants"]
    for i in range(2):
        b = rows["baseline"]["peaks"][i]
        p0 = rows["phase0"]["peaks"][i]
        p2 = rows["phase2"]["peaks"][i]
        assert b["relative_error"] > 0.99
        assert p0["signed"] < -0.999 and p0["relative_error"] > 1.65
        assert p2["signed"] > +0.999 and p2["relative_error"] < 0.31
    for wrong in (
        (low, baseline, phase0[:-1], phase2),
        (low, baseline, phase0, phase2[:-1]),
        (low, baseline, phase0, phase0),
        (low, baseline, baseline, phase2),
    ):
        try:
            check_paths_and_streams(*wrong)
        except ValueError:
            pass
        else:
            raise AssertionError("invalid A/B stream accepted")
    bad = phase2[:]
    bad[0] = float("nan")
    try:
        check_paths_and_streams(low, baseline, phase0, bad)
    except ValueError:
        pass
    else:
        raise AssertionError("nonfinite data accepted")
    print("PASS SBR-70: independent-path A/B geometry, anti-phase tone controls and NaN")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--self-test", action="store_true")
    for name in ("low", "reference", "baseline", "phase0", "phase2"):
        p.add_argument("--" + name)
    p.add_argument("--first-frame", type=int, default=3)
    p.add_argument("--last-frame", type=int, default=46)
    args = p.parse_args()
    if args.self_test:
        self_test()
        return 0
    if not all((args.low, args.reference, args.baseline,
                args.phase0, args.phase2)):
        p.error("--low --reference --baseline --phase0 --phase2 required")
    try:
        low, ref, baseline, phase0, phase2 = (
            load(filename) for filename in
            (args.low, args.reference, args.baseline, args.phase0, args.phase2))
        signal = check_paths_and_streams(low, baseline, phase0, phase2)
        lag, corr, _, gain, bias, _, _ = compare(low, ref)
        if not math.isfinite(corr) or corr < 0.98 or not math.isfinite(gain):
            raise ValueError("LOW reference anchor not reliable")
        results = evaluate(low, ref, {"baseline": baseline, "phase0": phase0,
                                      "phase2": phase2},
                           lag, gain, bias, args.first_frame, args.last_frame)
    except (OSError, ValueError) as e:
        p.exit(2, f"SBR-70 refused: {e}\n")
    print(f"SBR-70 independent QMF PCM A/B, frames={results['frames']}, "
          f"LOW lag={lag} corr={corr:.8f} gain={gain:.9g}")
    print(f"phase0-baseline RMS={signal['delta0_rms']:.8g}, "
          f"phase2-baseline RMS={signal['delta2_rms']:.8g}, "
          f"phase2-phase0 RMS={signal['phase_difference_rms']:.8g}")
    for name, variant in results["variants"].items():
        print(f"[{name}]")
        for row in variant["peaks"]:
            def fmt(value):
                return "undefined" if value is None else f"{value:.6f}"
            print(f"  center k={row['band']} {row['hz']:.1f}Hz: "
                  f"amplitude={fmt(row['amplitude'])} "
                  f"coherence={fmt(row['signed'])} "
                  f"relative_error={fmt(row['relative_error'])}")
        for band in variant["bands"]:
            print(f"  {band['start']}-{band['end']}Hz: "
                  f"amplitude={fmt(band['ratio'])} "
                  f"coherence={fmt(band['signed'])} "
                  f"relative_error={fmt(band['relative_error'])}")
    print("Not normative HE-AAC: reference is FFmpeg-minus-Sauti-LOW; "
          "independent experimental QMF/PCM preview only.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
