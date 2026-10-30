#!/usr/bin/env python3
"""Sweep YM shape 0C envelope periods, comparing both NASM variants to ST-Sound.

Offline reference experiment only. No production decoder/player modifications.
"""
import argparse
import subprocess
from pathlib import Path
from compare_ym_pcm import read_pcm, metrics, stsound_filter, envelope_rms
from generate_ym_reference import create


def measure(candidate_path, reference_path):
    ra, a = read_pcm(candidate_path)
    rb, b = read_pcm(reference_path)
    if ra != rb:
        raise ValueError("sample rates differ")
    a = [0.5 * v for v in stsound_filter(a)]
    n, rms, _, corr = metrics(a, b)
    _, _, _, envelope_corr = metrics(envelope_rms(a[:n], ra), envelope_rms(b[:n], ra))
    return corr, rms, envelope_corr


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--old", type=Path, required=True)
    p.add_argument("--pre-tick", type=Path, required=True)
    p.add_argument("--stsound", type=Path, required=True)
    p.add_argument("--output", type=Path, default=Path("build/ym-reference/period-sweep"))
    p.add_argument("--periods", type=int, nargs="+", default=[25, 50, 100, 200, 400, 800])
    args = p.parse_args()
    for period in args.periods:
        if not 1 <= period <= 65535:
            p.error("periods must be 1..65535")
    args.output.mkdir(parents=True, exist_ok=True)
    print("YM 0C period | old corr / RMS / contour | pre-tick corr / RMS / contour | corr delta / RMS delta", flush=True)
    for period in args.periods:
        stem = args.output / f"envelope_0c_period_{period}"
        song = stem.with_suffix(".ym")
        oldwav = Path(str(stem) + ".old.wav")
        prewav = Path(str(stem) + ".pre.wav")
        refwav = Path(str(stem) + ".stsound.wav")
        fixture = create("envelope_0c", envelope_period=period)
        # YM3 has 150 bytes per register, in register order after "YM3!".
        frames = (len(fixture) - 4) // 14
        actual = fixture[4 + 11*frames] | (fixture[4 + 12*frames] << 8)
        if actual != period:
            raise ValueError(f"fixture period mismatch: requested {period}, got {actual}")
        song.write_bytes(fixture)
        for program, out in ((args.old, oldwav), (args.pre_tick, prewav), (args.stsound, refwav)):
            subprocess.run([str(program), str(song), str(out)], check=True, stdout=subprocess.DEVNULL)
        old = measure(oldwav, refwav)
        pre = measure(prewav, refwav)
        print(f"{period:5d} | {old[0]:.6f} / {old[1]:.6f} / {old[2]:.6f} | "
              f"{pre[0]:.6f} / {pre[1]:.6f} / {pre[2]:.6f} | "
              f"{pre[0]-old[0]:+.6f} / {pre[1]-old[1]:+.6f}", flush=True)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError) as exc:
        raise SystemExit(f"YM period sweep failed: {exc}")
