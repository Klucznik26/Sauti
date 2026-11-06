#!/usr/bin/env python3
"""SBR-55: inspect periodic residuals of verified single-tone QMF exports.

For each SBR-54x tone, fit one complex FFmpeg->Sauti gain on the excited LOW
band after the first 64 global slots.  Then inspect the dimensionless ratio
Sauti/(gain*FFmpeg), including its dominant non-DC periodic residual bin.
This is diagnostic only: it cannot prescribe a production QMF correction.
"""
import argparse
import cmath
import math
from pathlib import Path

from sauti_he_aac_qmf_reference_audit import load_manifest, read_complex


def fit_and_profile(sauti, reference, start=64, max_harmonic=64):
    if len(sauti) != len(reference) or start >= len(sauti):
        raise ValueError("invalid QMF series range")
    x, y = sauti[start:], reference[start:]
    energy = math.fsum(abs(v) ** 2 for v in y)
    if energy <= 0 or not math.isfinite(energy):
        raise ValueError("zero/nonfinite reference energy")
    gain = sum((ref.conjugate() * value for value, ref in zip(x, y)), 0j) / energy
    if not math.isfinite(gain.real) or not math.isfinite(gain.imag) or gain == 0j:
        raise ValueError("invalid fitted gain")
    ratios = []
    for value, ref in zip(x, y):
        denominator = gain * ref
        if abs(denominator) <= 1e-30:
            raise ValueError("near-zero fitted reference sample")
        ratios.append(value / denominator)
    residual = [value - 1.0 for value in ratios]
    n = len(residual)
    if n < 8:
        raise ValueError("too few valid slots")
    limit = n // 2 if max_harmonic == 0 else min(max_harmonic, n // 2)
    spectrum = []
    for harmonic in range(1, limit + 1):
        component = sum(
            value * cmath.exp(-2j * math.pi * harmonic * index / n)
            for index, value in enumerate(residual)
        ) / n
        spectrum.append((abs(component), harmonic, component))
    peak = max(spectrum)
    magnitudes = [abs(value) for value in ratios]
    phases = [math.degrees(cmath.phase(value)) for value in ratios]
    rms = math.sqrt(math.fsum(abs(value) ** 2 for value in residual) / n)
    return {
        "slots": n,
        "gain": gain,
        "ratio_mag_min": min(magnitudes),
        "ratio_mag_max": max(magnitudes),
        "ratio_phase_min": min(phases),
        "ratio_phase_max": max(phases),
        "ratio_residual_rms": rms,
        "peak_amplitude": peak[0],
        "peak_harmonic": peak[1],
        "peak_phase": math.degrees(cmath.phase(peak[2])),
        "alternating": (peak[1] == n // 2 and
                        math.isclose(peak[0], rms, rel_tol=1e-6, abs_tol=1e-12)),
    }


def load_excited(directory, low):
    folder = directory / f"low{low}"
    sa_manifest = load_manifest(folder / "sauti.json")
    ff_manifest = load_manifest(folder / "ffmpeg.json")
    if sa_manifest["independent"] or not ff_manifest["independent"]:
        raise ValueError(f"LOW {low}: reference is not independent")
    for key in ("core_sha256", "sample_rate_core", "frame_count", "qmf_source",
                "first_frame_preroll", "bands", "layout"):
        if sa_manifest[key] != ff_manifest[key]:
            raise ValueError(f"LOW {low}: manifest mismatch {key}")
    frames = sa_manifest["frame_count"]
    sa = read_complex(folder / "sauti.qmf.f32le", frames)
    ff = read_complex(folder / "ffmpeg.qmf.f32le", frames)
    band = low - 2
    series = lambda values: [
        complex(values[(slot * 4 + band) * 2], values[(slot * 4 + band) * 2 + 1])
        for slot in range(frames * 32)
    ]
    return series(sa), series(ff)


def self_test():
    count = 128
    reference = [cmath.exp(2j * math.pi * 3 * index / count) for index in range(count)]
    gain = complex(0.25, -0.5)
    sauti = [gain * value * (1.0 + 0.01 * cmath.exp(2j * math.pi * 7 * index / count))
             for index, value in enumerate(reference)]
    result = fit_and_profile(sauti, reference, start=0, max_harmonic=16)
    assert result["peak_harmonic"] == 7
    assert result["ratio_residual_rms"] > 0.009
    try:
        fit_and_profile(sauti[:7], reference[:7], start=0)
    except ValueError:
        pass
    else:
        raise AssertionError("short QMF series accepted")
    alternating = [1.0 + 0.02 * (-1.0) ** index for index in range(count)]
    result = fit_and_profile(
        [gain * value * ratio for value, ratio in zip(reference, alternating)],
        reference, start=0, max_harmonic=0)
    assert result["alternating"] and result["peak_harmonic"] == count // 2
    print("PASS SBR-55 modulation audit: gain fit, periodic residual, invalid range")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input-dir", default="/tmp/sbr54x-tones")
    parser.add_argument("--start-slot", type=int, default=64)
    parser.add_argument("--max-harmonic", type=int, default=0,
                        help="0 means the complete spectrum through Nyquist (default)")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if args.start_slot < 6 or args.max_harmonic < 0:
        parser.error("--start-slot must be >=6 and --max-harmonic must be >=0")
    try:
        directory = Path(args.input_dir)
        print("SBR-55 modulation residual audit: diagnostic only; no decoder correction")
        for low in range(2, 6):
            sauti, reference = load_excited(directory, low)
            result = fit_and_profile(sauti, reference, args.start_slot, args.max_harmonic)
            gain = result["gain"]
            print(
                f"EXCITE LOW={low} slots={result['slots']} "
                f"gain=({gain.real:.9g},{gain.imag:.9g}) "
                f"ratio_mag=[{result['ratio_mag_min']:.9g},{result['ratio_mag_max']:.9g}] "
                f"ratio_phase_deg=[{result['ratio_phase_min']:.9g},{result['ratio_phase_max']:.9g}] "
                f"ratio_rms={result['ratio_residual_rms']:.9g} "
                f"peak_harmonic={result['peak_harmonic']} "
                f"peak_amplitude={result['peak_amplitude']:.9g} "
                f"peak_phase_deg={result['peak_phase']:.9g} "
                f"alternating_slot_residual={'yes' if result['alternating'] else 'no'}"
            )
    except (OSError, ValueError) as exc:
        parser.exit(2, f"SBR-55 modulation audit rejected: {exc}\n")


if __name__ == "__main__":
    main()
