#!/usr/bin/env python3
"""SBR-62: LOW-anchored, frame-resolved HF and noise contribution audit.

Diagnostic only: no modification of the AAC decoder or QMF synthesis.
FFmpeg minus the LOW reconstruction is a residual, NOT an isolated HF stem.
"""
import argparse
import math
from sauti_sbr61_hf_frame_audit import FRAME, _corr, _rms


def score(low, hf, noise, preview, reference, lag, gain, bias,
          first_frame=2, last_frame=None, min_ref_rms=1e-8):
    arrays = (low, hf, noise, preview, reference)
    if not low or len(low) % FRAME or any(len(x) != len(low) for x in (hf, noise, preview)):
        raise ValueError("all Sauti stems must have the same whole-frame length")
    if not all(math.isfinite(float(v)) for samples in arrays for v in samples):
        raise ValueError("nonfinite PCM value")
    if (not isinstance(lag, int) or first_frame < 0 or
        (last_frame is not None and last_frame < first_frame) or
        not all(math.isfinite(x) for x in (gain, bias, min_ref_rms)) or
        min_ref_rms <= 0.0):
        raise ValueError("invalid alignment or frame interval")

    # QMF synthesis is linear up to float roundoff. Refuse to interpret
    # misaligned, stale, mixed-channel or different-run component files.
    diff_sq = sum((float(p) - float(l) - float(h) - float(n)) ** 2
                  for l, h, n, p in zip(low, hf, noise, preview))
    closure_rms = math.sqrt(diff_sq / len(low))
    preview_rms = _rms([float(v) for v in preview])
    tolerance = max(1e-7, preview_rms * 1e-4)
    if closure_rms > tolerance:
        raise ValueError(f"component closure failed: RMS={closure_rms:.8g} > {tolerance:.8g}")

    counts = len(low) // FRAME
    end_frame = min(counts - 1, last_frame if last_frame is not None else counts - 1)
    records = []
    total_ref, total_hf, total_noise = [], [], []
    for frame in range(first_frame, end_frame + 1):
        lo = frame * FRAME
        hi = lo + FRAME
        if lo + lag < 0 or hi + lag > len(reference):
            continue
        r = [float(reference[i + lag]) - gain * float(low[i]) - bias
             for i in range(lo, hi)]
        h = [gain * float(hf[i]) for i in range(lo, hi)]
        n = [gain * float(noise[i]) for i in range(lo, hi)]
        if _rms(r) < min_ref_rms:
            records.append(dict(frame=frame, status="weak-reference"))
            continue
        both = [a + b for a, b in zip(h, n)]
        records.append(dict(frame=frame, status="measured",
                            ref_rms=_rms(r), hf_rms=_rms(h),
                            noise_rms=_rms(n), both_rms=_rms(both),
                            hf_corr=_corr(h, r), noise_corr=_corr(n, r),
                            both_corr=_corr(both, r)))
        total_ref.extend(r)
        total_hf.extend(h)
        total_noise.extend(n)
    if not total_ref:
        raise ValueError("no complete measurable reference-residual frames")
    combined = [a + b for a, b in zip(total_hf, total_noise)]
    metrics = {}
    for name, signal in (("HF", total_hf), ("noise", total_noise), ("HF+noise", combined)):
        metrics[name] = dict(corr=_corr(signal, total_ref),
                             rms=_rms(signal),
                             rel_error=_rms([a - b for a, b in zip(signal, total_ref)]) / _rms(total_ref))
    return dict(rows=records, closure_rms=closure_rms, closure_tolerance=tolerance,
                reference_rms=_rms(total_ref), metrics=metrics,
                measured=sum(r["status"] == "measured" for r in records),
                weak=sum(r["status"] == "weak-reference" for r in records))


def self_test():
    count = 9 * FRAME
    low = [0.22 * math.sin(i * 0.017) + 0.11 * math.sin(i * 0.031)
           for i in range(count)]
    hf = [0.006 * math.sin(i * 0.39) for i in range(count)]
    noise = [0.003 * math.sin(i * 0.73) for i in range(count)]
    preview = [a + b + c for a, b, c in zip(low, hf, noise)]
    lag, gain, bias = 384, 1.414, 0.003
    reference = [0.0] * lag + [gain * v + bias for v in preview] + [0.0] * FRAME
    good = score(low, hf, noise, preview, reference, lag, gain, bias)
    assert good["measured"] == 7 and good["weak"] == 0
    assert good["metrics"]["HF+noise"]["corr"] > 0.99999999
    assert good["metrics"]["HF+noise"]["rel_error"] < 1e-10
    assert good["metrics"]["HF"]["rel_error"] > 0.3
    assert good["metrics"]["noise"]["rel_error"] > 0.3

    # Changing the noise while preserving reconstruction closure must
    # reveal a genuine HF-vs-noise attribution difference.
    wrong_noise = [0.003 * math.sin(i * 0.87) for i in range(count)]
    wrong_preview = [a + b + c for a, b, c in zip(low, hf, wrong_noise)]
    wrong = score(low, hf, wrong_noise, wrong_preview, reference, lag, gain, bias)
    assert wrong["metrics"]["HF+noise"]["corr"] < good["metrics"]["HF+noise"]["corr"]

    broken_preview = preview[:]
    broken_preview[100] += 0.1
    for args in (
        (low, hf, noise, broken_preview, reference, lag, gain, bias),
        (low[:-1], hf, noise, preview, reference, lag, gain, bias),
    ):
        try:
            score(*args)
        except ValueError:
            pass
        else:
            raise AssertionError("bad stems must be rejected")
    bad_hf = hf[:]
    bad_hf[0] = math.nan
    try:
        score(low, bad_hf, noise, preview, reference, lag, gain, bias)
    except ValueError:
        pass
    else:
        raise AssertionError("nonfinite stem must be rejected")
    print("PASS SBR-62: component closure, reference HF/noise attribution, corrupt and NaN controls")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--self-test", action="store_true")
    p.add_argument("--low")
    p.add_argument("--hf")
    p.add_argument("--noise")
    p.add_argument("--preview")
    p.add_argument("--reference")
    p.add_argument("--first-frame", type=int, default=2)
    p.add_argument("--last-frame", type=int)
    p.add_argument("--all-frames", action="store_true")
    args = p.parse_args()
    if args.self_test:
        self_test()
        return 0
    if not all((args.low, args.hf, args.noise, args.preview, args.reference)):
        p.error("require --low --hf --noise --preview --reference")
    from sauti_he_aac_compare_pcm import compare, load
    try:
        low, hf, noise, preview, reference = (
            load(path) for path in (args.low, args.hf, args.noise,
                                   args.preview, args.reference))
        lag, corr, _, gain, bias, _, _ = compare(low, reference)
        if corr < 0.98 or not math.isfinite(corr):
            raise ValueError("LOW anchor correlation below 0.98")
        result = score(low, hf, noise, preview, reference, lag, gain, bias,
                       args.first_frame, args.last_frame)
    except (OSError, ValueError) as e:
        p.exit(2, f"SBR-62 refused: {e}\n")
    print(f"SBR-62 DIAGNOSTIC LOW anchor lag={lag} corr={corr:.8f} "
          f"gain={gain:.9g} bias={bias:.9g}")
    print(f"Component closure RMS={result['closure_rms']:.8g} "
          f"(limit={result['closure_tolerance']:.8g})")
    print(f"Reference residual RMS={result['reference_rms']:.8g} "
          f"frames measured={result['measured']} weak={result['weak']}")
    for name, stats in result["metrics"].items():
        c = stats["corr"]
        print(f"{name}: correlation={'undefined' if c is None else f'{c:.7f}'} "
              f"RMS={stats['rms']:.8g} relative_error={stats['rel_error']:.6f}")
    rows = result["rows"] if args.all_frames else (
        result["rows"][:3] + result["rows"][-3:])
    for row in rows:
        if row["status"] != "measured":
            print(f"frame={row['frame']:03d} {row['status']}")
            continue
        fmt = lambda v: "undefined" if v is None else f"{v:+.5f}"
        print(f"frame={row['frame']:03d} corrHF={fmt(row['hf_corr'])} "
              f"corrNoise={fmt(row['noise_corr'])} "
              f"corrBoth={fmt(row['both_corr'])} "
              f"RMSref={row['ref_rms']:.8g}")
    print("Only diagnostic attribution relative to FFmpeg-minus-LOW, NOT a normative HF stem.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
