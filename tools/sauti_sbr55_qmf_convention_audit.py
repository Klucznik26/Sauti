#!/usr/bin/env python3
"""SBR-55: fail-closed static audit of the 32-band analysis-QMF convention.

This diagnostic compares Sauti's 640-tap prototype with both FFmpeg windows
in a *specified* FFmpeg source tree: the 320-tap downsampled analysis window
and the full 640-tap synthesis window. It also requires the source-order
anchors used by FFmpeg's analysis path. The tool does not decode audio,
change PCM/QMF data, or establish HE-AAC conformance.

The reported coefficient relation is deliberately narrow:
    ffmpeg_ds[i] == sauti_prototype[2*i], i in [0, 319].
FFmpeg multiplies its window in reverse order while its x history is oldest
first; Sauti keeps history newest first.  Equal coefficients therefore rule
out a copied-table scale/sign discrepancy for either window, but not a
transform, channel, fixture, or PCM-scale discrepancy.
"""
import argparse
import math
import re
from pathlib import Path


SAUTI_TABLE = re.compile(
    r"static\s+const\s+float\s+qmf_coeff\s*\[\s*640\s*\]\s*=\s*\{(.*?)\};",
    re.DOTALL,
)
FLOAT = re.compile(r"[-+]?(?:\d+\.\d*|\.\d+)(?:[eE][-+]?\d+)?f")
Q31 = re.compile(r"(?P<outer>-)?\s*Q31\(\s*(?P<value>[-+]?(?:\d+\.\d*|\.\d+)(?:[eE][-+]?\d+)?f)\s*\)")


def without_comments(text):
    return re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.DOTALL)


def extract_sauti(text):
    match = SAUTI_TABLE.search(text)
    if not match:
        raise ValueError("Sauti 640-tap QMF table not found")
    values = [float(v[:-1]) for v in FLOAT.findall(without_comments(match.group(1)))]
    if len(values) != 640:
        raise ValueError(f"Sauti QMF table has {len(values)} values, expected 640")
    return values


def extract_ffmpeg(text, name, count):
    table = re.compile(
        rf"{re.escape(name)}\s*\)\s*\[\s*{count}\s*\]\s*=\s*\{{(.*?)\}};",
        re.DOTALL,
    )
    match = table.search(text)
    if not match:
        raise ValueError(f"FFmpeg {count}-tap QMF table {name} not found")
    values = []
    for item in Q31.finditer(without_comments(match.group(1))):
        value = float(item.group("value")[:-1])
        values.append(-value if item.group("outer") else value)
    if len(values) != count:
        raise ValueError(f"FFmpeg QMF table {name} has {len(values)} values, expected {count}")
    return values


def require_analysis_anchors(text):
    anchors = (
        "memcpy(x    , x+nb, (320-32)*sizeof(x[0]));",
        "memcpy(x+288, in,         nb*sizeof(x[0]));",
        "vector_fmul_reverse(z, sbr_qmf_window_ds, x, 320);",
        "sbrdsp->sum64x5(z);",
        "sbrdsp->qmf_pre_shuffle(z);",
        "sbrdsp->qmf_post_shuffle(W[buf_idx][i], z);",
    )
    missing = [anchor for anchor in anchors if anchor not in text]
    if missing:
        raise ValueError("unsupported FFmpeg analysis source; missing anchor: " + missing[0])


def compare(expected, ffmpeg):
    differences = [actual - wanted for actual, wanted in zip(ffmpeg, expected)]
    worst = max(range(320), key=lambda i: abs(differences[i]))
    squared = math.fsum(d * d for d in differences)
    return {
        "max_abs": abs(differences[worst]),
        "rms": math.sqrt(squared / len(differences)),
        "worst": worst,
        "actual": ffmpeg[worst],
        "expected": expected[worst],
    }


def self_test():
    values = [((i - 320) / 100000.0) for i in range(640)]
    source = "static const float qmf_coeff[640]={" + ",".join(f"{v:.10f}f" for v in values) + "};"
    reference = "static const DECLARE_ALIGNED(32, INTFLOAT, sbr_qmf_window_ds)[320] = {" + ",".join(f"Q31({v:.10f}f)" for v in values[::2]) + "};"
    full_reference = "static const DECLARE_ALIGNED(32, INTFLOAT, sbr_qmf_window_us)[640] = {" + ",".join(f"Q31({v:.10f}f)" for v in values) + "};"
    sauti = extract_sauti(source)
    assert compare(sauti[::2], extract_ffmpeg(reference, "sbr_qmf_window_ds", 320))["max_abs"] == 0.0
    assert compare(sauti, extract_ffmpeg(full_reference, "sbr_qmf_window_us", 640))["max_abs"] == 0.0
    unary = reference.replace("Q31(-0.0032000000f)", "-Q31(0.0032000000f)", 1)
    assert compare(sauti[::2], extract_ffmpeg(unary, "sbr_qmf_window_ds", 320))["max_abs"] == 0.0
    bad = unary.replace("-Q31(0.0032000000f)", "-Q31(0.0031000000f)", 1)
    assert math.isclose(
        compare(sauti[::2], extract_ffmpeg(bad, "sbr_qmf_window_ds", 320))["max_abs"],
        0.0001, rel_tol=0.0, abs_tol=1e-12
    )
    template = "\n".join((
        "memcpy(x    , x+nb, (320-32)*sizeof(x[0]));",
        "memcpy(x+288, in,         nb*sizeof(x[0]));",
        "dsp->vector_fmul_reverse(z, sbr_qmf_window_ds, x, 320);",
        "sbrdsp->sum64x5(z);", "sbrdsp->qmf_pre_shuffle(z);",
        "sbrdsp->qmf_post_shuffle(W[buf_idx][i], z);",
    ))
    require_analysis_anchors(template)
    try:
        require_analysis_anchors(template.replace("sum64x5", "sum64x4"))
    except ValueError:
        pass
    else:
        raise AssertionError("missing source anchor accepted")
    print("PASS SBR-56 QMF convention audit: analysis/synthesis extraction, signs, anchors")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--sauti", default="src/decoders/modern/aac/sauti_aac_sbr_qmf_synth_window.c")
    parser.add_argument("--ffmpeg-data", help="FFmpeg libavcodec/aacsbrdata.h")
    parser.add_argument("--ffmpeg-template", help="FFmpeg libavcodec/aacsbr_template.c")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if not args.ffmpeg_data or not args.ffmpeg_template:
        parser.error("--ffmpeg-data and --ffmpeg-template are required outside --self-test")
    try:
        sauti = extract_sauti(Path(args.sauti).read_text(encoding="utf-8"))
        ffmpeg_text = Path(args.ffmpeg_data).read_text(encoding="utf-8")
        analysis = extract_ffmpeg(ffmpeg_text, "sbr_qmf_window_ds", 320)
        synthesis = extract_ffmpeg(ffmpeg_text, "sbr_qmf_window_us", 640)
        require_analysis_anchors(Path(args.ffmpeg_template).read_text(encoding="utf-8"))
        analysis_result = compare(sauti[::2], analysis)
        synthesis_result = compare(sauti, synthesis)
    except (OSError, ValueError) as exc:
        parser.exit(2, f"SBR-55 QMF convention audit rejected: {exc}\n")
    print("SBR-56 QMF convention audit: diagnostic only; no decoder correction")
    print("mapping: FFmpeg ds[i] == Sauti prototype[2*i], i=0..319")
    print("analysis max_abs={max_abs:.9g} rms={rms:.9g} worst=i{worst} ffmpeg={actual:.9g} sauti={expected:.9g}".format(**analysis_result))
    print("synthesis mapping: FFmpeg us[i] == Sauti prototype[i], i=0..639")
    print("synthesis max_abs={max_abs:.9g} rms={rms:.9g} worst=i{worst} ffmpeg={actual:.9g} sauti={expected:.9g}".format(**synthesis_result))
    if analysis_result["max_abs"] != 0.0 or synthesis_result["max_abs"] != 0.0:
        parser.exit(1, "SBR-56 result: prototype mismatch; do not normalize or change production QMF\n")
    print("PASS SBR-56: identical decimal analysis and synthesis prototype taps")


if __name__ == "__main__":
    main()
