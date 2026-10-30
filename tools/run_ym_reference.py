#!/usr/bin/env python3
"""Run Sauti and StSound renders for deterministic YM3 reference fixtures."""
import argparse
from pathlib import Path
import subprocess
import sys

from generate_ym_reference import TRACKS, create


def run(command):
    subprocess.run([str(arg) for arg in command], check=True)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--sauti", type=Path, required=True)
    p.add_argument("--stsound", type=Path, required=True)
    p.add_argument("--output", type=Path, default=Path("build/ym-reference/controlled"))
    p.add_argument("--only", nargs="+", choices=TRACKS, help="run selected controlled tracks only")
    a = p.parse_args()
    script_dir = Path(__file__).resolve().parent
    compare = script_dir / "compare_ym_pcm.py"
    a.output.mkdir(parents=True, exist_ok=True)
    for name in (a.only if a.only else TRACKS):
        ym = a.output / (name + ".ym")
        sauti_wav = a.output / (name + ".sauti.wav")
        reference_wav = a.output / (name + ".stsound.wav")
        ym.write_bytes(create(name))
        print(f"\n=== {name} ===", flush=True)
        run([a.sauti, ym, sauti_wav])
        run([a.stsound, ym, reference_wav])
        cmd = [sys.executable, compare, sauti_wav, reference_wav, "--segments", "--segment-step", "0.5"]
        if name.startswith("envelope"):
            cmd.append("--envelope-rms")
        if name == "envelope_restart":
            cmd.extend(["--transition", "1"])
        run(cmd)


if __name__ == "__main__":
    try:
        main()
    except (OSError, subprocess.CalledProcessError) as exc:
        print(f"Reference batch failed: {exc}", file=sys.stderr)
        sys.exit(1)
