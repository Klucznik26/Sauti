#!/usr/bin/env python3
"""Measure constant-volume PSG tone transfer curves against ST-Sound.

The same 3-second YM3 fixture is rendered at each of the 16 fixed mixer
volumes. Report AC RMS (subtracting mean) to avoid conflating DC bias with
signal amplitude; never modify normal playback paths.
"""
import argparse
import math
import subprocess
from pathlib import Path

from generate_ym_reference import create
from compare_ym_pcm import read_pcm, stsound_filter


def ac_rms(values):
    if not values:
        raise ValueError("empty PCM")
    mean = sum(values) / len(values)
    return math.sqrt(sum((value - mean) ** 2 for value in values) / len(values))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sauti", type=Path, required=True)
    parser.add_argument("--stsound", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=Path("build/ym-reference/dac-levels"))
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)

    measurements = []
    for level in range(16):
        stem = args.output / f"tone_volume_{level:02d}"
        song = Path(str(stem) + ".ym")
        swav = Path(str(stem) + ".sauti.wav")
        rwav = Path(str(stem) + ".stsound.wav")
        fixture = create("tone_fixed", fixed_volume=level)
        frames = (len(fixture) - 4) // 14
        if fixture[4 + 8 * frames] != level:
            raise ValueError(f"wrong fixture level at volume {level}")
        song.write_bytes(fixture)
        for binary, output in ((args.sauti, swav), (args.stsound, rwav)):
            subprocess.run([str(binary), str(song), str(output)], check=True, stdout=subprocess.DEVNULL)
        rs, sauti = read_pcm(swav)
        rr, reference = read_pcm(rwav)
        if rs != rr:
            raise ValueError("sample rate mismatch")
        n = min(len(sauti), len(reference))
        # Mirror previous filtered, half-gain envelope experiments.
        experimental = [v * 0.5 for v in stsound_filter(sauti)]
        sauti_ac = ac_rms(experimental[:n])
        reference_ac = ac_rms(reference[:n])
        ratio = reference_ac / sauti_ac if sauti_ac else float("nan")
        measurements.append((level, sauti_ac, reference_ac, ratio))
    print("YM fixed tone DAC transfer (filtered Sauti x0.5 vs ST-Sound)")
    print("volume | Sauti AC RMS | ST-Sound AC RMS | reference/Sauti")
    for level, ours, theirs, ratio in measurements:
        print(f"  {level:2d}   | {ours:.8f} | {theirs:.8f} | {ratio:.6f}")
    print("Caution: 4-bit fixed-volume table, NOT a direct measurement of the 32-step envelope.")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        raise SystemExit(f"YM DAC sweep failed: {error}")
