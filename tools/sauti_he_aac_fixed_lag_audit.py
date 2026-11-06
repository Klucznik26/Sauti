#!/usr/bin/env python3
"""SBR-54n: compare LOW proxy, HF-only candidate, and FFmpeg at ONE lag.

This is a *diagnostic*, not an audio decoder or MPEG-4 compliance test.
It does not scale/clamp/normalize any PCM. Only fitted statistics are
reported, never substituted into playback.
"""
import argparse
import array
import math
import os
import sys


def load(path):
    size = os.path.getsize(path)
    if size == 0 or size % 4:
        raise ValueError(f"{path}: expected nonempty f32le samples")
    result = array.array("f")
    with open(path, "rb") as handle:
        result.frombytes(handle.read())
    if sys.byteorder != "little":
        result.byteswap()
    if not all(math.isfinite(value) for value in result):
        raise ValueError(f"{path}: nonfinite PCM")
    return result


def align(base, candidate, reference, lag):
    """Same timebase: baseline[i], candidate[i], reference[i+lag]."""
    first = max(0, -lag)
    end = min(len(base), len(candidate), len(reference) - lag)
    if end - first < 2048:
        raise ValueError("less than 2048 aligned samples at the chosen lag")
    return (
        [float(base[i]) for i in range(first, end)],
        [float(candidate[i]) for i in range(first, end)],
        [float(reference[i + lag]) for i in range(first, end)],
    )


def rms_and_peak(values):
    n = len(values)
    return (
        math.sqrt(math.fsum(v * v for v in values) / n),
        max(abs(v) for v in values),
    )


def linear_fit(x, y):
    """Least-squares y = gain*x + bias, same aligned sample indices."""
    n = len(x)
    mx = math.fsum(x) / n
    my = math.fsum(y) / n
    xx = math.fsum((v - mx) ** 2 for v in x)
    yy = math.fsum((v - my) ** 2 for v in y)
    xy = math.fsum((a - mx) * (b - my) for a, b in zip(x, y))
    if xx <= 0.0 or yy <= 0.0:
        raise ValueError("silent or constant data cannot be correlated")
    gain = xy / xx
    offset = my - gain * mx
    err = math.sqrt(
        math.fsum((gain * a + offset - b) ** 2
                  for a, b in zip(x, y)) / n
    )
    ref_rms = math.sqrt(math.fsum(v * v for v in y) / n)
    return {
        "corr": xy / math.sqrt(xx * yy),
        "gain": gain,
        "offset": offset,
        "error": err,
        "relative": err / ref_rms,
    }


def hf_added_value(base, candidate, reference, base_fit):
    """Orthogonalize added HF against LOW and DC.

    Fit the best *additional* signed coefficient for candidate-base to
    the reference residual from the best LOW-only fit. This tells whether
    the HF contains new reference-correlated information, rather than
    merely being loud. It must NEVER be used as a playback gain fix.
    """
    n = len(base)
    delta = [c - b for c, b in zip(candidate, base)]
    db = math.fsum(delta) / n
    bb = math.fsum(base) / n
    b_centered = [b - bb for b in base]
    d_centered = [d - db for d in delta]
    var_b = math.fsum(v * v for v in b_centered)
    if var_b <= 0.0:
        raise ValueError("LOW baseline has no varying signal")
    projection = (
        math.fsum(d * b for d, b in zip(d_centered, b_centered)) /
        var_b
    )
    hf_unique = [d - projection * b
                 for d, b in zip(d_centered, b_centered)]
    norm = math.fsum(v * v for v in hf_unique)
    residual = [y - base_fit["gain"] * b - base_fit["offset"]
                for b, y in zip(base, reference)]
    err_energy = math.fsum(v * v for v in residual)
    if err_energy <= 0.0:
        return delta, None
    if norm <= max(1e-25 * n, 1e-20 * var_b):
        return delta, None
    cross = math.fsum(a * b for a, b in zip(hf_unique, residual))
    optimal_hf_gain = cross / norm
    remaining = max(0.0, err_energy - cross * cross / norm)
    return delta, {
        "projection": optimal_hf_gain,
        "residual_correlation": cross / math.sqrt(norm * err_energy),
        "residual_explained": (cross * cross / norm) / err_energy,
        "best_error": math.sqrt(remaining / n),
    }


def audit(baseline, candidate, reference, lag):
    base, cand, ref = align(baseline, candidate, reference, lag)
    fit_base = linear_fit(base, ref)
    fit_cand = linear_fit(cand, ref)
    delta, additional = hf_added_value(base, cand, ref, fit_base)
    return {
        "n": len(base),
        "lag": lag,
        "base": rms_and_peak(base),
        "candidate": rms_and_peak(cand),
        "reference": rms_and_peak(ref),
        "delta": rms_and_peak(delta),
        "base_fit": fit_base,
        "candidate_fit": fit_cand,
        "additional": additional,
    }


def show(label, stats):
    print(f"{label}: corr={stats['corr']:.6f}; "
          f"fit_gain={stats['gain']:.8g}; "
          f"error_RMS={stats['error']:.9g}; "
          f"relative_error={stats['relative']:.3%}")


def synthetic_test():
    n = 8192
    low = [0.25 * math.sin(i * 0.027) +
           0.08 * math.cos(i * 0.099) for i in range(n)]
    hf = [0.045 * math.sin(i * 0.202) for i in range(n)]
    candidate = [a + b for a, b in zip(low, hf)]
    true_pcm = [a + 0.7 * b for a, b in zip(low, hf)]
    # At lag -6, reference[i-6] equals the unshifted true_pcm[i].
    reference = true_pcm[6:] + [0.0] * 6
    result = audit(low, candidate, reference, -6)
    assert result["n"] == n - 6
    assert result["candidate_fit"]["relative"] <            result["base_fit"]["relative"]
    assert result["candidate_fit"]["corr"] >            result["base_fit"]["corr"]
    assert result["additional"] is not None
    assert result["additional"]["residual_explained"] > 0.99
    assert abs(result["additional"]["projection"] - 0.7) < 0.01
    assert result["additional"]["best_error"] < 1e-6
    print("SBR-54n: fixed-lag aligned positive HF-residual self-test PASS")
    no_hf = audit(low, low, reference, -6)
    assert no_hf["additional"] is None
    print("SBR-54n: zero independent HF safely detected PASS")
    try:
        audit(low, candidate, reference, 9000)
    except ValueError:
        print("SBR-54n: invalid overlap rejection PASS")
    else:
        raise AssertionError("invalid alignment accepted")
    print("PASS SBR-54n fixed-lag HF diagnostic (no PCM modifications)")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--lag", type=int, default=-6,
                        help="Fixed reference shift in samples; default -6 "
                             "from the previous LOW/FFmpeg alignment")
    parser.add_argument("baseline", nargs="?",
                        help="LOW-only (or explicitly labeled LOW proxy) f32le")
    parser.add_argument("candidate", nargs="?",
                        help="SBR-54m refhf.f32le candidate")
    parser.add_argument("reference", nargs="?",
                        help="independent FFmpeg f32le")
    args = parser.parse_args()
    if args.self_test:
        synthetic_test()
        return 0
    if not all((args.baseline, args.candidate, args.reference)):
        parser.error("baseline candidate reference paths are required")
    try:
        result = audit(load(args.baseline), load(args.candidate),
                       load(args.reference), args.lag)
    except (OSError, ValueError) as exc:
        print(f"SBR-54n audit error: {exc}", file=sys.stderr)
        return 2
    print(f"SBR-54n: SAME fixed lag={result['lag']} samples "
          f"({result['lag']/48:.3f} ms at 48 kHz); "
          f"overlap={result['n']}; NO automatic lag search")
    for name in ("base", "candidate", "reference", "delta"):
        power, peak = result[name]
        print(f"  {name}: RMS={power:.9g}, peak={peak:.9g}")
    show("  baseline", result["base_fit"])
    show("  new G-only HF", result["candidate_fit"])
    extra = result["additional"]
    if extra is None:
        print("  HF residual: no independent HF variation after LOW+DC")
    else:
        print(
            f"  HF vs LOW-fit residual: correlation="
            f"{extra['residual_correlation']:.6f}, "
            f"max_residual_variance_explained="
            f"{extra['residual_explained']:.3%}, "
            f"optimal_ADDITIONAL_coefficient="
            f"{extra['projection']:.8g}, "
            f"theoretical_best_error_RMS={extra['best_error']:.9g}"
        )
    if result["candidate"][1] > 1.0:
        print("WARNING: candidate exceeds float PCM [-1,1]; "
              "do not clamp/normalize before diagnosis")
    print("Diagnostic only: fitting gains are NOT codec corrections; "
          "LOW proxy is not a clean isolated LOW stem.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
