#!/usr/bin/env python3
"""SBR-61: per-frame HIGH+noise residual audit with a LOW-only alignment.

Diagnostic only: this never changes AAC/SBR decoding, gains or PCM output.
The reference residual after LOW also contains LOW-model error; this is not an
isolated normative HE-AAC high-band reference.
"""
import argparse
import math

FRAME = 2048  # HE-AAC 48 kHz PCM per 1024-sample AAC-LC frame


def _rms(values):
    return math.sqrt(sum(x * x for x in values) / len(values))


def _corr(a, b):
    n = len(a)
    ma = sum(a) / n
    mb = sum(b) / n
    aa = sum((v - ma) ** 2 for v in a)
    bb = sum((v - mb) ** 2 for v in b)
    if aa <= 0.0 or bb <= 0.0:
        return None
    return sum((x - ma) * (y - mb) for x, y in zip(a, b)) / math.sqrt(aa * bb)


def audit_frames(low, candidate, reference, lag, gain, bias,
                 skip_frames=2, min_reference_rms=1e-8):
    """Keep LOW-derived lag/gain/bias fixed; never optimize them on HF."""
    if not low or len(low) != len(candidate) or len(low) % FRAME:
        raise ValueError("LOW and candidate must contain equal, whole 2048-sample frames")
    if not isinstance(lag, int) or skip_frames < 0 or not (
        math.isfinite(gain) and math.isfinite(bias) and
        math.isfinite(min_reference_rms) and min_reference_rms > 0.0
    ):
        raise ValueError("invalid LOW anchor or audit parameters")
    if not all(math.isfinite(float(x)) for data in (low, candidate, reference) for x in data):
        raise ValueError("NaN/Inf in PCM input")
    results = []
    joined_ref = []
    joined_candidate = []
    for frame in range(skip_frames, len(low) // FRAME):
        start = frame * FRAME
        end = start + FRAME
        if start + lag < 0 or end + lag > len(reference):
            continue  # never compare a partial frame
        ref = [float(reference[i + lag]) - gain * float(low[i]) - bias
               for i in range(start, end)]
        hf = [gain * (float(candidate[i]) - float(low[i]))
              for i in range(start, end)]
        ref_rms = _rms(ref)
        hf_rms = _rms(hf)
        if ref_rms < min_reference_rms:
            results.append(dict(frame=frame, ref_rms=ref_rms, hf_rms=hf_rms,
                                corr=None, relative_error=None, status="weak-reference"))
            continue
        correlation = _corr(hf, ref)
        error = _rms([a - b for a, b in zip(hf, ref)]) / ref_rms
        results.append(dict(frame=frame, ref_rms=ref_rms, hf_rms=hf_rms,
                            corr=correlation, relative_error=error,
                            status="measured" if correlation is not None else "zero-candidate"))
        joined_ref.extend(ref)
        joined_candidate.extend(hf)
    if not results or not joined_ref:
        raise ValueError("no complete frames with measurable reference HF residual")
    combined_corr = _corr(joined_candidate, joined_ref)
    relative_error = _rms([a - b for a, b in zip(joined_candidate, joined_ref)]) / _rms(joined_ref)
    return dict(rows=results, lag=lag, gain=gain, bias=bias,
                measured=sum(r["status"] == "measured" for r in results),
                weak=sum(r["status"] == "weak-reference" for r in results),
                combined_corr=combined_corr, combined_relative_error=relative_error,
                combined_reference_rms=_rms(joined_ref),
                combined_candidate_rms=_rms(joined_candidate))


def self_test():
    count = 12 * FRAME
    low = [0.24 * math.sin(i * 0.037) + 0.09 * math.sin(i * 0.011)
           for i in range(count)]
    hf = [0.004 * math.sin(i * 0.337) + 0.003 * math.sin(i * 0.407)
          for i in range(count)]
    lag, gain, bias = 384, 1.25, 0.003
    full = [a + b for a, b in zip(low, hf)]
    ref = [0.0] * lag + [gain * (a + b) + bias
                         for a, b in zip(low, hf)] + [0.0] * FRAME
    good = audit_frames(low, full, ref, lag, gain, bias)
    assert good["measured"] == 10 and good["weak"] == 0
    assert good["combined_corr"] > 0.99999999
    assert good["combined_relative_error"] < 1e-10
    # A near-perfect FULL-wave correlation MUST NOT hide wrong HF phase.
    wrong = [a + 0.005 * math.sin(i * 0.53) for i, a in enumerate(low)]
    bad = audit_frames(low, wrong, ref, lag, gain, bias)
    assert bad["combined_corr"] is not None and abs(bad["combined_corr"]) < 0.2
    assert bad["combined_relative_error"] > 0.5
    # A reference-silent frame is explicitly excluded, never reported PASS.
    silent_ref = ref[:]
    for i in range(5 * FRAME, 6 * FRAME):
        silent_ref[i + lag] = gain * low[i] + bias
    limited = audit_frames(low, full, silent_ref, lag, gain, bias)
    assert limited["weak"] == 1 and limited["measured"] == 9
    for args in ((low[:-1], full, ref, lag, gain, bias),
                 (low, full, [], lag, gain, bias)):
        try:
            audit_frames(*args)
        except ValueError:
            pass
        else:
            raise AssertionError("invalid geometry must be rejected")
    nan_full = full[:]
    nan_full[4] = float("nan")
    try:
        audit_frames(low, nan_full, ref, lag, gain, bias)
    except ValueError:
        pass
    else:
        raise AssertionError("NaN must be rejected")
    print("PASS SBR-61: LOW-anchored HF frames, wrong-HF control, silence and invalid inputs")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--low", help="SBR-54c LOW-only f32le PCM")
    parser.add_argument("--candidate", help="SBR-60 full diagnostic preview f32le PCM")
    parser.add_argument("--reference", help="independent FFmpeg f32le PCM, same channel")
    parser.add_argument("--skip-frames", type=int, default=2)
    parser.add_argument("--min-reference-rms", type=float, default=1e-8)
    parser.add_argument("--all-frames", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    if not (args.low and args.candidate and args.reference):
        parser.error("--low, --candidate and --reference are required")
    from sauti_he_aac_compare_pcm import compare, load
    try:
        low, candidate, ref = load(args.low), load(args.candidate), load(args.reference)
        lag, corr, _, gain, bias, _, _ = compare(low, ref)
        if not math.isfinite(corr) or corr < 0.98 or not math.isfinite(gain):
            raise ValueError("LOW reference anchor is weak; refusing to classify HF")
        out = audit_frames(low, candidate, ref, lag, gain, bias,
                           args.skip_frames, args.min_reference_rms)
    except (ValueError, OSError) as exc:
        parser.exit(2, f"SBR-61 refused: {exc}\n")
    print(f"SBR-61 DIAGNOSTIC (not HE-AAC conformance): LOW lag={lag}, "
          f"corr={corr:.7f}, fixed gain={gain:.9g}, bias={bias:.9g}")
    print(f"Frames measured={out['measured']}, weak-reference={out['weak']}; "
          f"reference residual RMS={out['combined_reference_rms']:.9g}, "
          f"candidate HF+noise RMS={out['combined_candidate_rms']:.9g}")
    cc = out["combined_corr"]
    print(f"HF+noise-only: corr={cc:.7f} (undefined if zero)" if cc is not None
          else "HF+noise-only: correlation undefined (zero candidate)")
    print(f"HF+noise-only relative RMS error={out['combined_relative_error']:.6f}")
    measured = [r for r in out["rows"] if r["status"] == "measured"]
    if args.all_frames:
        rows = out["rows"]
    else:
        # Worst-correlated frames, and separately expose all unmeasurable frames.
        rows = sorted(measured, key=lambda r: r["corr"])[:8] + [
            r for r in out["rows"] if r["status"] != "measured"]
    for r in rows:
        cv = "undefined" if r["corr"] is None else f"{r['corr']:+.5f}"
        ev = "undefined" if r["relative_error"] is None else f"{r['relative_error']:.5f}"
        print(f"frame={r['frame']:03d} status={r['status']} "
              f"ref_rms={r['ref_rms']:.8g} cand_rms={r['hf_rms']:.8g} "
              f"corr={cv} rel_error={ev}")
    print("LOW-model residual is not an isolated FFmpeg HF stem. "
          "Do not apply a gain/phase compensation based on this diagnostic.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
