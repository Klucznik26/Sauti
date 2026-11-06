#!/usr/bin/env python3
"""SBR-66: fixed-LOW A/B of already-exported HE-AAC harmonic/phase paths.

This compares experimental PCM previews, not normative decoded HF stems.
No FFT-bin alignment, gain, phase, or timing is fitted per candidate.
"""
import argparse
import math

from sauti_he_aac_compare_pcm import compare, load
from sauti_sbr63_hf_spectral_audit import FRAME, RATE, fft

BIN_HZ = RATE / FRAME
# Exact nominal QMF-band centers (k+1/2)*375 Hz for target k=18,42.
TARGETS = ((18, 6937.5), (42, 15937.5))
REGIONS = ((6750, 7125), (8625, 9000), (12000, 15000),
           (15375, 16875))


def evaluate(low, reference, variants, lag, gain, bias, first_frame=3, last_frame=46):
    if not low or len(low) % FRAME or first_frame < 0 or last_frame < first_frame:
        raise ValueError("invalid whole-frame geometry or frame range")
    if last_frame >= len(low) // FRAME or not isinstance(lag, int) or not (
            math.isfinite(gain) and math.isfinite(bias)):
        raise ValueError("invalid LOW anchor or frame selection")
    if not variants or "baseline" not in variants:
        raise ValueError("baseline preview is mandatory")
    for label, signal in variants.items():
        if len(signal) != len(low):
            raise ValueError(f"{label}: candidates must match LOW frame geometry")
    if not all(math.isfinite(float(x)) for a in
               [low, reference, *variants.values()] for x in a):
        raise ValueError("NaN or Infinity in PCM")
    for band, hz in TARGETS:
        assert abs(hz / 375.0 - (band + 0.5)) < 1e-12
        assert abs(hz / BIN_HZ - round(hz / BIN_HZ)) < 1e-12

    window = [0.5 - 0.5 * math.cos(2 * math.pi * i / (FRAME - 1))
              for i in range(FRAME)]
    accum = {
        name: {
            "cross": [0j] * (FRAME // 2 + 1),
            "power": [0.0] * (FRAME // 2 + 1),
            "difference": [0.0] * (FRAME // 2 + 1)
        }
        for name in variants
    }
    ref_power = [0.0] * (FRAME // 2 + 1)
    frames = 0
    for frame in range(first_frame, last_frame + 1):
        start = frame * FRAME
        if start + lag < 0 or start + lag + FRAME > len(reference):
            raise ValueError("incomplete reference frame at LOW-derived lag")
        ref_f = fft([(float(reference[start + lag + j]) -
                      gain * float(low[start + j]) - bias) * window[j]
                     for j in range(FRAME)])
        for k, r in enumerate(ref_f):
            ref_power[k] += abs(r)**2
        for name, pcm in variants.items():
            hf_f = fft([gain * (float(pcm[start + j]) -
                                float(low[start + j])) * window[j]
                        for j in range(FRAME)])
            for k, h in enumerate(hf_f):
                r = ref_f[k]
                accum[name]["power"][k] += abs(h)**2
                accum[name]["difference"][k] += abs(h - r)**2
                accum[name]["cross"][k] += h * r.conjugate()
        frames += 1
    if frames != last_frame - first_frame + 1 or sum(ref_power) <= 0:
        raise ValueError("no measurable complete frames")
    rows = {}
    for name, measurements in accum.items():
        bands = []
        for start_hz, end_hz in REGIONS:
            lo = math.ceil(start_hz / BIN_HZ)
            hi = math.ceil(end_hz / BIN_HZ)
            rp = sum(ref_power[lo:hi])
            hp = sum(measurements["power"][lo:hi])
            er = sum(measurements["difference"][lo:hi])
            cross = sum(measurements["cross"][lo:hi])
            bands.append(dict(start=start_hz, end=end_hz,
                              ref_fraction=rp / sum(ref_power),
                              ratio=math.sqrt(hp / rp) if rp else None,
                              signed=cross.real / math.sqrt(hp * rp)
                                     if hp > 0 and rp > 0 else None,
                              relative_error=math.sqrt(er / rp) if rp else None))
        peaks = []
        for qmf_band, hz in TARGETS:
            k = round(hz / BIN_HZ)
            rp = ref_power[k]
            hp = measurements["power"][k]
            er = measurements["difference"][k]
            cross = measurements["cross"][k]
            peaks.append(dict(band=qmf_band, hz=hz,
                              amplitude=math.sqrt(hp/rp) if rp else None,
                              relative_error=math.sqrt(er/rp) if rp else None,
                              signed=cross.real/math.sqrt(hp*rp)
                                     if hp > 0 and rp > 0 else None))
        rows[name] = dict(bands=bands, peaks=peaks)
    return dict(frames=frames, variants=rows)


def self_test():
    count = 8 * FRAME
    low = [0.22 * math.sin(2*math.pi*800*i/RATE) for i in range(count)]
    t18 = [0.008*math.sin(2*math.pi*6937.5*i/RATE) for i in range(count)]
    t42 = [0.005*math.sin(2*math.pi*15937.5*i/RATE) for i in range(count)]
    lag, gain, bias = 384, 1.25, 0.001
    ref_hf = [a+b for a,b in zip(t18,t42)]
    reference = [0.0]*lag + [
        gain*(l+h)+bias for l,h in zip(low,ref_hf)] + [0.0]*FRAME
    shifted = [
        l+0.008*math.sin(2*math.pi*(6937.5+70.3125)*i/RATE) +
        0.005*math.sin(2*math.pi*(15937.5-445.3125)*i/RATE)
        for i,l in enumerate(low)]
    corrected = [l+h for l,h in zip(low,ref_hf)]
    result = evaluate(low, reference,
                      {"baseline": shifted, "sine": corrected},
                      lag, gain, bias, 2, 6)
    assert result["frames"] == 5
    good, bad = result["variants"]["sine"], result["variants"]["baseline"]
    for g, b in zip(good["peaks"],bad["peaks"]):
        assert g["relative_error"] < 1e-8 and g["signed"] > 0.999999
        assert b["relative_error"] > 0.9
    for label, a in (("bad length", {"baseline": shifted[:-1]}),
                     ("missing baseline", {"sine": corrected})):
        try:
            evaluate(low,reference,a,lag,gain,bias,2,6)
        except ValueError:
            pass
        else:
            raise AssertionError(label)
    broken = corrected[:]
    broken[0] = float("nan")
    try:
        evaluate(low,reference,{"baseline":broken},lag,gain,bias,2,6)
    except ValueError:
        pass
    else:
        raise AssertionError("nonfinite PCM accepted")
    print("PASS SBR-66: exact nominal QMF centers, displaced peaks, A/B and fail-closed")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--self-test", action="store_true")
    p.add_argument("--low")
    p.add_argument("--reference")
    p.add_argument("--baseline")
    p.add_argument("--sine")
    p.add_argument("--units")
    p.add_argument("--gonly")
    p.add_argument("--conj")
    p.add_argument("--first-frame", type=int, default=3)
    p.add_argument("--last-frame", type=int, default=46)
    args = p.parse_args()
    if args.self_test:
        self_test()
        return 0
    if not all((args.low, args.reference, args.baseline, args.sine)):
        p.error("--low, --reference, --baseline and --sine are required")
    try:
        low, reference = load(args.low), load(args.reference)
        pcm = {"baseline": load(args.baseline), "sine":load(args.sine)}
        for key in ("units", "gonly", "conj"):
            value = getattr(args,key)
            if value is not None:
                pcm[key] = load(value)
        lag, low_corr, _, gain, bias, _, _ = compare(low, reference)
        if not math.isfinite(low_corr) or low_corr < 0.98 or not math.isfinite(gain):
            raise ValueError("weak LOW alignment: no HF comparison allowed")
        result = evaluate(low,reference,pcm,lag,gain,bias,
                          args.first_frame,args.last_frame)
    except (OSError,ValueError) as err:
        p.exit(2,f"SBR-66 refused: {err}\n")
    print(f"SBR-66 DIAGNOSTIC: {result['frames']} frames, fixed LOW lag={lag}, "
          f"corr={low_corr:.8f}, gain={gain:.9g}, bias={bias:.9g}")
    baseline = result["variants"]["baseline"]
    for name, row in result["variants"].items():
        print(f"[{name}]")
        for index, target in enumerate(row["peaks"]):
            before = baseline["peaks"][index]["relative_error"]
            after = target["relative_error"]
            change = before - after if before is not None and after is not None else float("nan")
            print(f"  QMF k={target['band']} center={target['hz']:.1f}Hz "
                  f"amplitude={target['amplitude']:.5f} "
                  f"coherence={target['signed']:.5f} "
                  f"rel_error={after:.6f} change_vs_baseline={change:+.6f}")
        for band in row["bands"]:
            print(f"  {band['start']}-{band['end']}Hz signed={band['signed']:.5f} "
                  f"rel_error={band['relative_error']:.5f} "
                  f"amplitude={band['ratio']:.5f}")
    print("SBR-66 compares EXPERIMENTAL signal paths, not normative HE-AAC. "
          "FFmpeg-minus-LOW contains noise and LOW residual error. "
          "No production fixes or HF-specific fitting.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
