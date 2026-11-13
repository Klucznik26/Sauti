#!/usr/bin/env python3
"""Isolate audible SBR-vs-PS errors on real HE-AAC v2 ADTS audio.

Runs 4 independent native experimental previews through the SAME PS engine:
(1) full provisional HF+ROM noise, (2) provisional signal HF only,
(3) raw transposed HF without gain/noise, (4) low QMF only. Compares each with FFmpeg independently. Diagnostic
only, none of the previews is an ISO compliant HE-AAC reconstruction.
"""
import argparse
import subprocess
import sys
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--out-dir", type=Path,
                        default=Path("/tmp/sauti_ps_isolation"))
    parser.add_argument("--native", default="./build/sauti_ps5_temporal_audit")
    parser.add_argument("--rate", type=int, default=48000)
    args = parser.parse_args()
    if not args.source.is_file():
        parser.error("AAC input does not exist")
    if args.rate <= 0:
        parser.error("--rate must be positive")
    args.out_dir.mkdir(parents=True, exist_ok=True)
    modes = (
        ("full", "--experimental-pcm"),
        ("no_noise", "--experimental-pcm-no-noise"),
        ("raw_hf", "--experimental-pcm-raw-hf"),
        ("low_only", "--experimental-pcm-low-only"),
    )
    compare = Path(__file__).with_name("sauti_ps5_compare.py")
    for name, option in modes:
        output = args.out_dir / (name + ".f32le")
        # Never leave a stale preview looking like fresh output.
        output.unlink(missing_ok=True)
        print(f"\\n=== {name} ===", flush=True)
        command = [args.native, str(args.source), option, str(output)]
        res = subprocess.run(command, capture_output=True, text=True,
                             check=False)
        if res.returncode:
            print(res.stderr[-2500:], file=sys.stderr)
            print(f"FAIL: native mode {name} ({res.returncode})",file=sys.stderr)
            return 1
        if not output.is_file() or output.stat().st_size == 0:
            print(f"FAIL: empty PCM output in {name}",file=sys.stderr)
            return 1
        print("PCM:", output, "size:", output.stat().st_size, "bytes")
        for line in res.stderr.splitlines():
            if line.startswith("HF-POWER "):
                print(line)
        metrics = subprocess.run(
            [sys.executable, str(compare), str(args.source), "--pcm",
             str(output), "--rate", str(args.rate)], check=False)
        if metrics.returncode:
            print(f"FAIL: reference comparison for {name}",file=sys.stderr)
            return metrics.returncode
    print("\\nDIAGNOSTIC COMPLETE. Compare RMS, tone fractions and channel "
          "isolation between the four modes; no audio fidelity PASS implied.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
