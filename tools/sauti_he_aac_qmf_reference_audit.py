#!/usr/bin/env python3
"""SBR-54w: strict independent complex LOW-QMF comparison gate.

Input files: little-endian complex64 interleaved Re,Im for FOUR bands
2..5, frame-major (32 slots/frame, 4 bands/slot, 2 floats/bin).
Both files MUST be exported from the SAME AAC-LC core PCM input.
An independent reference export (e.g. instrumented FFmpeg QMF) is
REQUIRED. Do not pass another Sauti export as an independent reference.
Only complete frames are accepted. The first frame's first six slots
are excluded because W[i-6] is unavailable there.
"""
import argparse
import array
import json
import math
import pathlib
import sys

BANDS = (2, 3, 4, 5)
WORDS_PER_FRAME = 32 * len(BANDS) * 2
FORMAT = "sauti-sbr54w-low-qmf-complex32-v1"


def load_manifest(path):
    obj = json.loads(pathlib.Path(path).read_text(encoding="utf-8"))
    required = ("format", "producer", "independent", "core_sha256",
                "sample_rate_core", "frame_count", "qmf_source",
                "first_frame_preroll", "bands", "layout")
    if any(key not in obj for key in required):
        raise ValueError("missing manifest fields")
    if obj["format"] != FORMAT or obj["bands"] != list(BANDS) or (
        obj["layout"] != "frame-slot-band-re-im-f32le"
    ) or obj["qmf_source"] != "W[i-6]" or obj["first_frame_preroll"] != 6:
        raise ValueError("unsupported QMF geometry/layout")
    if type(obj["frame_count"]) is not int or obj["frame_count"] < 2:
        raise ValueError("at least two complete frames required")
    if type(obj["sample_rate_core"]) is not int or obj["sample_rate_core"] <= 0:
        raise ValueError("invalid core sample rate")
    sha = obj["core_sha256"]
    if not isinstance(sha, str) or len(sha) != 64 or any(c not in "0123456789abcdef" for c in sha):
        raise ValueError("invalid core SHA256")
    if not isinstance(obj["producer"], str) or not obj["producer"]:
        raise ValueError("missing producer")
    if type(obj["independent"]) is not bool:
        raise ValueError("independent must be a boolean")
    return obj


def read_complex(path, frames):
    buf = pathlib.Path(path).read_bytes()
    if len(buf) != frames * WORDS_PER_FRAME * 4:
        raise ValueError("QMF binary size does not match manifest frame_count")
    a = array.array("f")
    a.frombytes(buf)
    if sys.byteorder != "little":
        a.byteswap()
    if not all(math.isfinite(x) for x in a):
        raise ValueError("NaN/Inf in complex QMF")
    return a


def compare(a, b, frames, first_valid=6):
    results = []
    for band in range(4):
        aa, bb = [], []
        for frame in range(frames):
            for slot in range(first_valid if frame == 0 else 0, 32):
                ix = ((frame * 32 + slot) * 4 + band) * 2
                aa.append(complex(a[ix], a[ix+1]))
                bb.append(complex(b[ix], b[ix+1]))
        energy_a = math.fsum(abs(z)**2 for z in aa)
        energy_b = math.fsum(abs(z)**2 for z in bb)
        cross = sum((x.conjugate()*y for x, y in zip(aa, bb)), 0j)
        err = math.fsum(abs(x-y)**2 for x, y in zip(aa, bb))
        if energy_a <= 0 or energy_b <= 0:
            raise ValueError(f"silent QMF band {band+2}: no valid phase/correlation")
        coherence = cross / math.sqrt(energy_a * energy_b)
        results.append({
            "band": band+2, "samples": len(aa),
            "sa_energy": energy_a, "ref_energy": energy_b,
            "energy_ratio": energy_a / energy_b,
            "coherence_re": coherence.real,
            "coherence_im": coherence.imag,
            "abs_coherence": abs(coherence),
            "rms_error": math.sqrt(err / len(aa)),
            "relative_error": math.sqrt(err / energy_b),
        })
    return results


def run(sa_bin, sa_manifest, ref_bin, ref_manifest):
    sa = load_manifest(sa_manifest)
    ref = load_manifest(ref_manifest)
    if sa["independent"] or not ref["independent"] or (
        sa["producer"] == ref["producer"]
    ):
        raise ValueError("independent reference producer required, not Sauti-vs-Sauti")
    for field in ("core_sha256", "sample_rate_core", "frame_count",
                  "qmf_source", "first_frame_preroll", "bands", "layout"):
        if sa[field] != ref[field]:
            raise ValueError(f"manifest mismatch: {field}")
    a = read_complex(sa_bin, sa["frame_count"])
    b = read_complex(ref_bin, ref["frame_count"])
    return compare(a, b, sa["frame_count"])


def self_test():
    import tempfile
    with tempfile.TemporaryDirectory() as tmp:
        p = pathlib.Path(tmp)
        frames = 2
        a = array.array("f")
        for frame in range(frames):
            for slot in range(32):
                for band in range(4):
                    a.extend((float(band+1),float(slot+1)))
        if sys.byteorder != "little":
            a.byteswap()
        (p/"sa.bin").write_bytes(a.tobytes())
        (p/"ref.bin").write_bytes(a.tobytes())
        metadata = {"format": FORMAT, "producer": "Sauti", "independent": False,
                    "core_sha256": "a"*64, "sample_rate_core": 24000,
                    "frame_count": frames, "qmf_source": "W[i-6]",
                    "first_frame_preroll": 6, "bands": list(BANDS),
                    "layout": "frame-slot-band-re-im-f32le"}
        (p/"sa.json").write_text(json.dumps(metadata))
        metadata["producer"] = "independent-test"
        metadata["independent"] = True
        (p/"ref.json").write_text(json.dumps(metadata))
        result=run(p/"sa.bin",p/"sa.json",p/"ref.bin",p/"ref.json")
        assert all(v["samples"] == 58 and v["rms_error"] == 0 and
                   abs(v["coherence_re"]-1)<1e-12 for v in result)
        metadata["core_sha256"] = "b"*64
        (p/"ref.json").write_text(json.dumps(metadata))
        try:
            run(p/"sa.bin",p/"sa.json",p/"ref.bin",p/"ref.json")
        except ValueError as exc:
            assert "core_sha256" in str(exc)
        else:
            raise AssertionError("mismatched core accepted")
        metadata["core_sha256"] = "a"*64
        (p/"ref.json").write_text(json.dumps(metadata))
        (p/"ref.bin").write_bytes((p/"ref.bin").read_bytes()[:-4])
        try:
            run(p/"sa.bin",p/"sa.json",p/"ref.bin",p/"ref.json")
        except ValueError as exc:
            assert "size" in str(exc)
        else:
            raise AssertionError("truncated reference accepted")
    print("PASS SBR-54w strict complex QMF comparison gate (synthetic ONLY)")


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("sauti_bin", nargs="?")
    parser.add_argument("sauti_manifest", nargs="?")
    parser.add_argument("reference_bin", nargs="?")
    parser.add_argument("reference_manifest", nargs="?")
    args=parser.parse_args()
    try:
        if args.self_test:
            self_test()
            return 0
        if any(x is None for x in (args.sauti_bin,args.sauti_manifest,
                                    args.reference_bin,args.reference_manifest)):
            parser.error("provide Sauti QMF binary+manifest and independent reference binary+manifest")
        results=run(args.sauti_bin,args.sauti_manifest,args.reference_bin,args.reference_manifest)
        print("SBR-54w: complex W[i-6] independent QMF comparison; NOT PCM")
        for r in results:
            print(f"  LOW={r['band']} n={r['samples']} "
                  f"energy_ratio={r['energy_ratio']:.9g} "
                  f"complex_coherence=({r['coherence_re']:.9g},{r['coherence_im']:.9g}) "
                  f"abs_coherence={r['abs_coherence']:.9g} "
                  f"diff_RMS={r['rms_error']:.9g} "
                  f"diff_rel={r['relative_error']:.9g}")
        return 0
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"SBR-54w rejected: {exc}",file=sys.stderr)
        return 2


if __name__=="__main__":
    raise SystemExit(main())
