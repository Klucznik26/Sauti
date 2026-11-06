#!/usr/bin/env python3
"""SBR-64: inspect 375-Hz (QMF-width) frequency buckets at HF band edges.

Not an isolated normative reference HF stem: FFmpeg-minus-LOW also includes
reference noise and LOW errors. Diagnostic only, no decoder changes.
"""
import argparse
import math

from sauti_he_aac_compare_pcm import compare, load
from sauti_sbr62_hf_stem_audit import score
from sauti_sbr63_hf_spectral_audit import FRAME, RATE, fft

BUCKET_HZ = 375
BUCKETS = 64
REF_FRACTION_MIN = 0.0001  # 0.01% of full reference-residual FFT power


def audit(low, hf, reference, lag, gain, bias, first_frame=3, last_frame=46):
    if not low or len(low) != len(hf) or len(low) % FRAME:
        raise ValueError("LOW/HF streams must contain equal complete frames")
    if not isinstance(lag, int) or not (math.isfinite(gain) and math.isfinite(bias)):
        raise ValueError("invalid fixed LOW anchor")
    if first_frame < 0 or last_frame < first_frame or last_frame >= len(low) // FRAME:
        raise ValueError("invalid frame selection")
    if not all(math.isfinite(float(v)) for signal in (low, hf, reference) for v in signal):
        raise ValueError("nonfinite PCM input")

    window = [0.5 - 0.5 * math.cos(2.0 * math.pi * i / (FRAME - 1))
              for i in range(FRAME)]
    totals = [dict(ref=0.0, hf=0.0, error=0.0, cross=0j)
              for _ in range(BUCKETS)]
    frames = 0
    for frame in range(first_frame, last_frame + 1):
        base = frame * FRAME
        if base + lag < 0 or base + lag + FRAME > len(reference):
            raise ValueError("reference has incomplete shifted frame")
        reference_pcm = [
            (float(reference[base + lag + i]) -
             gain * float(low[base + i]) - bias) * window[i]
            for i in range(FRAME)]
        candidate_pcm = [gain * float(hf[base + i]) * window[i]
                         for i in range(FRAME)]
        ref_fft = fft(reference_pcm)
        cand_fft = fft(candidate_pcm)
        for k in range(FRAME // 2 + 1):
            bucket = min(BUCKETS - 1, int((k * RATE / FRAME) // BUCKET_HZ))
            r, h = ref_fft[k], cand_fft[k]
            row = totals[bucket]
            row["ref"] += abs(r) ** 2
            row["hf"] += abs(h) ** 2
            row["error"] += abs(h - r) ** 2
            row["cross"] += h * r.conjugate()
        frames += 1

    full_ref = sum(row["ref"] for row in totals)
    if full_ref <= 0.0 or not math.isfinite(full_ref):
        raise ValueError("empty reference residual")
    rows = []
    for i, row in enumerate(totals):
        ref_fraction = row["ref"] / full_ref
        cand_fraction = row["hf"] / full_ref
        denom = math.sqrt(row["ref"] * row["hf"])
        signed = row["cross"].real / denom if denom > 0.0 else None
        if ref_fraction >= REF_FRACTION_MIN:
            status = "REF_ACTIVE"
        elif cand_fraction >= REF_FRACTION_MIN:
            status = "EXTRA_CANDIDATE"
        else:
            status = "LOW_ENERGY"
        rows.append(dict(index=i, start=i * BUCKET_HZ,
                         end=(i + 1) * BUCKET_HZ, status=status,
                         ref_fraction=ref_fraction,
                         cand_fraction=cand_fraction,
                         error_fraction=row["error"] / full_ref,
                         amplitude_ratio=math.sqrt(row["hf"] / row["ref"])
                                         if row["ref"] else None,
                         signed_coherence=signed,
                         relative_error=math.sqrt(row["error"] / row["ref"])
                                        if row["ref"] else None))
    return dict(rows=rows, frames=frames, ref_energy=full_ref)


def self_test():
    count = 10 * FRAME
    low = [0.2 * math.sin(2 * math.pi * 850 * i / RATE)
           for i in range(count)]
    freqs = ((20, 0.010), (30, 0.008), (44, 0.004))
    parts = {
        band: [amp * math.sin(2 * math.pi * (band + 0.5) * BUCKET_HZ * i / RATE)
               for i in range(count)]
        for band, amp in freqs
    }
    hf = [sum(parts[band][i] for band, _ in freqs)
          for i in range(count)]
    lag, gain, bias = 384, 1.3, 0.003
    ref = [0.0] * lag + [
        gain * (l + h) + bias for l, h in zip(low, hf)
    ] + [0.0] * FRAME
    good = audit(low, hf, ref, lag, gain, bias, 2, 8)
    assert good["frames"] == 7
    for index in parts:
        row = good["rows"][index]
        assert row["status"] == "REF_ACTIVE" and row["signed_coherence"] > 0.999999
        assert row["relative_error"] < 1e-8
    bad_hf = [parts[20][i] + parts[30][i] - parts[44][i]
              for i in range(count)]
    bad = audit(low, bad_hf, ref, lag, gain, bias, 2, 8)
    assert bad["rows"][20]["signed_coherence"] > 0.999999
    assert bad["rows"][30]["signed_coherence"] > 0.999999
    assert bad["rows"][44]["signed_coherence"] < -0.999999
    assert bad["rows"][44]["relative_error"] > 1.99
    # An invented component where reference has none is not called 'weak PASS'.
    extra = [v + 0.007 * math.sin(2 * math.pi * 19500 * i / RATE)
             for i, v in enumerate(hf)]
    spurious = audit(low, extra, ref, lag, gain, bias, 2, 8)
    assert any(row["status"] == "EXTRA_CANDIDATE"
               for row in spurious["rows"][50:55])
    for first, last in ((9, 8), (0, 20)):
        try:
            audit(low, hf, ref, lag, gain, bias, first, last)
        except ValueError:
            pass
        else:
            raise AssertionError("invalid frame range accepted")
    print("PASS SBR-64: QMF-width edge buckets, inverted band, extra HF and invalid ranges")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--self-test", action="store_true")
    p.add_argument("--low")
    p.add_argument("--hf")
    p.add_argument("--noise")
    p.add_argument("--preview")
    p.add_argument("--reference")
    p.add_argument("--first-frame", type=int, default=3)
    p.add_argument("--last-frame", type=int, default=46)
    p.add_argument("--all-bands", action="store_true")
    args = p.parse_args()
    if args.self_test:
        self_test()
        return 0
    if not all((args.low, args.hf, args.noise, args.preview, args.reference)):
        p.error("require --low --hf --noise --preview --reference")
    try:
        low, hf, noise, preview, ref = (
            load(path) for path in (args.low, args.hf, args.noise,
                                   args.preview, args.reference))
        lag, low_corr, _, gain, bias, _, _ = compare(low, ref)
        if not math.isfinite(low_corr) or low_corr < 0.98:
            raise ValueError("LOW reference alignment is too weak")
        closure = score(low, hf, noise, preview, ref, lag, gain, bias,
                        args.first_frame, args.last_frame)
        measurement = audit(low, hf, ref, lag, gain, bias,
                            args.first_frame, args.last_frame)
    except (OSError, ValueError) as exc:
        p.exit(2, f"SBR-64 refused: {exc}\n")

    print(f"SBR-64 DIAGNOSTIC: frames={measurement['frames']} LOW lag={lag}, "
          f"corr={low_corr:.8f}, gain={gain:.8f}, "
          f"closure_RMS={closure['closure_rms']:.9g}")
    print("FFT bucket width 375 Hz (QMF-width only, not isolated complex QMF bands).")
    print("Band Hz | status | reference share | candidate share | "
          "amplitude ratio | signed coherence | relative error | absolute error share")
    rows = measurement["rows"]
    if args.all_bands:
        selected = rows
    else:
        edges = [r for r in rows if 6000 <= r["start"] < 9000 or
                 15000 <= r["start"] < 18000]
        strongest = sorted(rows, key=lambda r: r["error_fraction"], reverse=True)[:10]
        selected = sorted({r["index"]: r for r in edges + strongest}.values(),
                          key=lambda r: r["index"])
    for r in selected:
        fmt = lambda v: "undefined" if v is None else f"{v:.5f}"
        print(f"{r['start']:5d}-{r['end']:5d} {r['status']:15s} "
              f"{r['ref_fraction']:.5f} {r['cand_fraction']:.5f} "
              f"{fmt(r['amplitude_ratio'])} {fmt(r['signed_coherence'])} "
              f"{fmt(r['relative_error'])} {r['error_fraction']:.6f}")
    print("Diagnostic FFmpeg-minus-LOW residual is not a normative reference HF stem. "
          "No per-band fitting or decoder correction.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
