#!/usr/bin/env python3
"""SBR-65: pinpoint HF spectral peaks and their displacement vs FFmpeg-minus-LOW.

Diagnostic only. The FFmpeg-minus-LOW residual is NOT an isolated normative
SBR HF stem. Does not tune HF phase, amplitude, frequency or decoder state.
"""
import argparse
import math

from sauti_he_aac_compare_pcm import compare, load
from sauti_sbr62_hf_stem_audit import score
from sauti_sbr63_hf_spectral_audit import FRAME, RATE, fft

FFT_BIN_HZ = RATE / FRAME
REGIONS = ((6000, 9000), (15000, 18000))


def _peaks(powers, lo, hi, count=4, suppress=3):
    """Choose independent maxima, rather than adjacent Hann-window bins."""
    ranked = sorted(range(lo, hi), key=lambda k: powers[k], reverse=True)
    selected = []
    for k in ranked:
        if all(abs(k - old) > suppress for old in selected):
            selected.append(k)
            if len(selected) >= count:
                break
    return selected


def analyze(low, hf, reference, lag, gain, bias, first_frame=3, last_frame=46):
    if not low or len(low) != len(hf) or len(low) % FRAME:
        raise ValueError("LOW/HF must contain the same number of whole frames")
    if not isinstance(lag, int) or not (math.isfinite(gain) and math.isfinite(bias)):
        raise ValueError("invalid fixed LOW anchor")
    if first_frame < 0 or last_frame < first_frame or last_frame >= len(low) // FRAME:
        raise ValueError("invalid frame selection")
    if not all(math.isfinite(float(v)) for signal in (low, hf, reference) for v in signal):
        raise ValueError("non-finite PCM input")

    nfreq = FRAME // 2 + 1
    powers_r = [0.0] * nfreq
    powers_h = [0.0] * nfreq
    cross = [0j] * nfreq
    win = [0.5 - 0.5 * math.cos(2 * math.pi * i / (FRAME - 1))
           for i in range(FRAME)]
    for frame in range(first_frame, last_frame + 1):
        start = frame * FRAME
        if start + lag < 0 or start + lag + FRAME > len(reference):
            raise ValueError("reference does not cover full shifted frame")
        r = fft([(float(reference[start + lag + i]) -
                  gain * float(low[start + i]) - bias) * win[i]
                 for i in range(FRAME)])
        h = fft([gain * float(hf[start + i]) * win[i]
                 for i in range(FRAME)])
        for k in range(nfreq):
            powers_r[k] += abs(r[k]) ** 2
            powers_h[k] += abs(h[k]) ** 2
            cross[k] += h[k] * r[k].conjugate()

    all_ref = sum(powers_r)
    if not math.isfinite(all_ref) or all_ref <= 0.0:
        raise ValueError("reference residual is silent")
    result = []
    for start_hz, end_hz in REGIONS:
        lo = math.ceil(start_hz / FFT_BIN_HZ)
        hi = min(nfreq, math.ceil(end_hz / FFT_BIN_HZ))
        if hi <= lo:
            raise ValueError("empty analysis region")
        total_r = sum(powers_r[lo:hi])
        total_h = sum(powers_h[lo:hi])
        if total_r <= all_ref * 1e-8:
            raise ValueError("reference region too weak")
        ref_peaks = _peaks(powers_r, lo, hi)
        hf_peaks = _peaks(powers_h, lo, hi)
        ref_peak = ref_peaks[0]
        hf_peak = hf_peaks[0]
        denom = math.sqrt(powers_r[ref_peak] * powers_h[ref_peak])
        ref_bin_coherence = (
            cross[ref_peak].real / denom if denom else None
        )
        result.append(dict(
            start_hz=start_hz, end_hz=end_hz,
            ref_region_fraction=total_r / all_ref,
            hf_ref_region_rms_ratio=math.sqrt(total_h / total_r),
            ref_peaks=[(k * FFT_BIN_HZ, powers_r[k] / total_r) for k in ref_peaks],
            hf_peaks=[(k * FFT_BIN_HZ, powers_h[k] / total_h)
                      for k in hf_peaks] if total_h else [],
            dominant_ref_hz=ref_peak * FFT_BIN_HZ,
            dominant_hf_hz=hf_peak * FFT_BIN_HZ if total_h else None,
            peak_shift_hz=(hf_peak - ref_peak) * FFT_BIN_HZ if total_h else None,
            ref_bin_signed_coherence=ref_bin_coherence,
            hf_power_at_ref_peak_fraction=(
                powers_h[ref_peak] / total_h if total_h else None),
        ))
    return result


def self_test():
    count = 10 * FRAME
    low = [0.22 * math.sin(2 * math.pi * 850 * i / RATE) for i in range(count)]
    # Exact FFT-bin-centered signals avoid ambiguous self-test peaks.
    ref_k1, ref_k2 = 299, 678
    hf_k1, hf_k2 = 302, 670
    tone = lambda k, i: math.sin(2 * math.pi * k * i / FRAME)
    ref_hf = [0.01 * tone(ref_k1, i) + 0.008 * tone(ref_k2, i)
              for i in range(count)]
    candidate_hf = [0.01 * tone(hf_k1, i) + 0.008 * tone(hf_k2, i)
                    for i in range(count)]
    lag, gain, bias = 384, 1.25, 0.001
    ref_pcm = [0.0] * lag + [
        gain * (l + h) + bias for l, h in zip(low, ref_hf)
    ] + [0.0] * FRAME
    measured = analyze(low, candidate_hf, ref_pcm, lag, gain, bias, 2, 8)
    assert len(measured) == 2
    for row, expected_ref, expected_hf in zip(
            measured, (ref_k1, ref_k2), (hf_k1, hf_k2)):
        assert abs(row["dominant_ref_hz"] - expected_ref * FFT_BIN_HZ) < 0.1
        assert abs(row["dominant_hf_hz"] - expected_hf * FFT_BIN_HZ) < 0.1
        assert abs(row["peak_shift_hz"] - (expected_hf - expected_ref) * FFT_BIN_HZ) < 0.1
        assert row["ref_peaks"][0][1] > 0.6
        assert row["hf_peaks"][0][1] > 0.6
    # When signals agree, dominant peaks coincide and coherence is positive.
    perfect = analyze(low, ref_hf, ref_pcm, lag, gain, bias, 2, 8)
    for row in perfect:
        assert row["peak_shift_hz"] == 0.0
        assert row["ref_bin_signed_coherence"] > 0.999999
    for first, last in ((8, 7), (0, 11)):
        try:
            analyze(low, candidate_hf, ref_pcm, lag, gain, bias, first, last)
        except ValueError:
            pass
        else:
            raise AssertionError("invalid frame selection accepted")
    bad = candidate_hf[:]
    bad[3] = float("nan")
    try:
        analyze(low, bad, ref_pcm, lag, gain, bias, 2, 8)
    except ValueError:
        pass
    else:
        raise AssertionError("NaN accepted")
    print("PASS SBR-65: HF dominant peak shifts, exact controls and invalid inputs")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--self-test", action="store_true")
    for name in ("low", "hf", "noise", "preview", "reference"):
        p.add_argument("--" + name)
    p.add_argument("--first-frame", type=int, default=3)
    p.add_argument("--last-frame", type=int, default=46)
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
        lag, corr, _, gain, bias, _, _ = compare(low, ref)
        if not math.isfinite(corr) or corr < 0.98:
            raise ValueError("LOW alignment correlation below 0.98")
        closure = score(low, hf, noise, preview, ref, lag, gain, bias,
                        args.first_frame, args.last_frame)
        rows = analyze(low, hf, ref, lag, gain, bias,
                       args.first_frame, args.last_frame)
    except (OSError, ValueError) as exc:
        p.exit(2, f"SBR-65 refused: {exc}\n")
    print(f"SBR-65 DIAGNOSTIC: frames={args.last_frame - args.first_frame + 1}, "
          f"LOW lag={lag}, corr={corr:.8f}, gain={gain:.8g}, "
          f"closure_RMS={closure['closure_rms']:.9g}; FFT bin={FFT_BIN_HZ:.5f} Hz")
    for row in rows:
        print(f"{row['start_hz']}-{row['end_hz']} Hz: "
              f"reference_energy_share={row['ref_region_fraction']:.6f} "
              f"candidate/ref_RMS={row['hf_ref_region_rms_ratio']:.6f}")
        print("  REFERENCE peaks: " + ", ".join(
            f"{hz:.2f} Hz ({fraction:.2%} of band power)"
            for hz, fraction in row["ref_peaks"]))
        print("  SAUTI HF peaks: " + ", ".join(
            f"{hz:.2f} Hz ({fraction:.2%} of band power)"
            for hz, fraction in row["hf_peaks"]))
        coh = row["ref_bin_signed_coherence"]
        print(f"  Dominant peak shift={row['peak_shift_hz']:+.2f} Hz; "
              f"candidate power at reference peak="
              f"{row['hf_power_at_ref_peak_fraction']:.5%}; "
              f"coherence at exact reference peak="
              f"{coh:.6f}" if coh is not None else
              "  Dominant reference peak not measurable in candidate")
    print("Diagnostic only: peak displacement is NOT proof of wrong QMF patch "
          "mapping; next validation requires complex HF QMF reference.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
