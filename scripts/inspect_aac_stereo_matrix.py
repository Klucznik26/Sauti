#!/usr/bin/env python3
"""AAC-29: reproducible, fail-closed 3x3x4 stereo decoding matrix.

Generate ORIGINAL synthetic audio -> encode with FFmpeg CLI ->
decode natively in Sauti -> compare independently against FFmpeg PCM.
The generated files and a JSON summary remain in build/audio-fixtures/aac-29.
"""
import argparse
import itertools
import json
from pathlib import Path
import shutil
import subprocess
import sys

HERE = Path(__file__).resolve().parent.parent
RATES = (32000, 44100, 48000)
BITRATES = (64, 128, 192, 256)
KINDS = ("tonal", "transient", "dense")


def invoke(command, cwd=HERE):
    return subprocess.run([str(arg) for arg in command], cwd=cwd,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          text=True, check=False)


def combinations(mode):
    if mode == "smoke":
        return (("tonal", 32000, 64), ("transient", 44100, 128),
                ("dense", 48000, 256))
    return itertools.product(KINDS, RATES, BITRATES)



def failure_detail(entry):
    """Report precise channels/metrics without hiding partial coverage."""
    metrics = entry.get("metrics", {})
    if metrics.get("reason"):
        return str(metrics["reason"])
    channels = metrics.get("channels", {})
    bad = []
    for label, values in channels.items():
        if not values.get("ok", False):
            bad.append("%s corr=%.9f RMS=%.6f nRMSE=%.6f max=%.7g" %
                       (label, values["corr"], values["ratio"],
                        values["nrmse"], values["max_error"]))
    if bad:
        return "; ".join(bad)
    return entry.get("error", "")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--mode", choices=("smoke", "full"), default="full")
    ap.add_argument("--output", type=Path,
                    default=Path("build/audio-fixtures/aac-29"))
    ap.add_argument("--disable-pns", action="store_true",
                    help="control experiment: use FFmpeg encoder with -aac_pns 0")
    opts = ap.parse_args(argv)
    if shutil.which("ffmpeg") is None:
        print("FAIL AAC-29: ffmpeg CLI is required", file=sys.stderr)
        return 2
    if not (HERE / "build/test_aac_real_stereo").is_file():
        print("FAIL AAC-29: build/test_aac_real_stereo is missing",
              file=sys.stderr)
        return 2
    root = opts.output
    if opts.disable_pns:
        root = root / "encoder-pns-off"
    if not root.is_absolute():
        root = HERE / root
    root.mkdir(parents=True, exist_ok=True)
    fixtures = root / "source"
    summary = []
    cases = tuple(combinations(opts.mode))
    required_rates = {rate for _, rate, _ in cases}
    for rate in sorted(required_rates):
        gen = invoke([sys.executable, HERE / "tools/generate_aac_stereo_fixtures.py",
                      fixtures / str(rate), rate])
        if gen.returncode:
            print(gen.stdout, file=sys.stderr)
            return 2
    for number, (kind, rate, kbps) in enumerate(cases, 1):
        label = "%s_%s_%sk" % (kind, rate, kbps)
        base = root / label
        wav = fixtures / str(rate) / (kind + ".wav")
        aac = base.with_suffix(".aac")
        native = base.with_suffix(".native.f32")
        reference = base.with_suffix(".reference.f32")
        mask = base.with_suffix(".mask")
        cmd_encode = ["ffmpeg", "-hide_banner", "-nostdin", "-loglevel", "error",
                      "-y", "-i", wav, "-ac", "2", "-ar", str(rate),
                      "-c:a", "aac", "-b:a", "%sk" % kbps] +
                     (["-aac_pns", "0"] if opts.disable_pns else []) +
                     ["-f", "adts", aac]
        cmd_ref = ["ffmpeg", "-hide_banner", "-nostdin", "-loglevel", "error",
                   "-y", "-i", aac, "-ac", "2", "-ar", str(rate),
                   "-f", "f32le", reference]
        commands = (
            cmd_encode,
            cmd_ref,
            [HERE / "build/test_aac_real_stereo", aac, native, mask],
            [sys.executable, HERE / "tools/verify_aac_stereo_strict.py",
             native, reference, mask, "--json"],
        )
        entry = {"name": label, "material": kind, "rate": rate,
                 "bitrate_kbps": kbps, "ok": False}
        output = []
        for step, command in zip(("encode", "reference", "native", "verify"),
                                 commands):
            proc = invoke(command)
            output.append("== %s ==\n%s" % (step, proc.stdout))
            if step == "verify":
                try:
                    entry["metrics"] = json.loads(proc.stdout)
                    entry["ok"] = bool(entry["metrics"].get("ok")) and not proc.returncode
                except json.JSONDecodeError:
                    entry["error"] = "verifier did not return valid JSON"
            if proc.returncode:
                entry.setdefault("error", "%s returned %d" %
                                 (step, proc.returncode))
                break
        base.with_suffix(".log").write_text("\n".join(output), encoding="utf-8")
        summary.append(entry)
        verdict = "PASS" if entry["ok"] else "FAIL"
        detail = failure_detail(entry) if not entry["ok"] else ""
        print("[%02d/%02d] %-26s %s %s" %
              (number, len(cases), label, verdict, detail), flush=True)
    report = {"mode": opts.mode, "pns_enabled_in_encoder": not opts.disable_pns,
              "count": len(summary),
              "passed": sum(x["ok"] for x in summary),
              "failed": sum(not x["ok"] for x in summary),
              "cases": summary}
    report_file = root / ("summary_" + opts.mode + ".json")
    report_file.write_text(json.dumps(report, indent=2, sort_keys=True),
                           encoding="utf-8")
    print("AAC-29: %d/%d PASS; details: %s" %
          (report["passed"], report["count"], report_file))
    return 0 if report["failed"] == 0 and len(summary) == len(cases) else 1


if __name__ == "__main__":
    sys.exit(main())
