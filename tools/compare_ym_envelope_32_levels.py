#!/usr/bin/env python3
"""Diagnostic 32-step YM 0C envelope amplitude table against ST-Sound.

Uses a deliberately slow envelope and a fast tone, so each envelope step
contains many tone cycles. Measures central AC RMS per expected step, not the
exact per-level hardware DAC voltage. Filtering can smear step boundaries.
"""
import argparse
import math
import subprocess
from pathlib import Path

from compare_ym_pcm import read_pcm, stsound_filter
from generate_ym_reference import create, FRAMES

RATE = 44100
CLOCK = 2000000


def ac_rms(values):
    if not values:
        raise ValueError("empty analysis window")
    mean = sum(values) / len(values)
    return math.sqrt(sum((x - mean)**2 for x in values) / len(values))


def fixture(period):
    data = bytearray(create("envelope_0c", envelope_period=period))
    for frame in range(FRAMES):
        data[4 + frame] = 16   # Fast tone: several cycles per envelope level
        data[4 + FRAMES + frame] = 0
    if data[4 + 11*FRAMES] | (data[4 + 12*FRAMES] << 8) != period:
        raise ValueError("fixture envelope period mismatch")
    return bytes(data)


def samples_per_step(period):
    step_q16 = (CLOCK * 65536) // (8 * RATE)
    return (period * 65536) / step_q16


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--sauti", type=Path, required=True)
    p.add_argument("--stsound", type=Path, required=True)
    p.add_argument("--period", type=int, default=1500)
    p.add_argument("--output", type=Path, default=Path("build/ym-reference/envelope-32-levels"))
    a = p.parse_args()
    if not 1 <= a.period <= 65535:
        p.error("period must be 1..65535")
    duration = samples_per_step(a.period)
    if duration < 100 or duration*32 > FRAMES*RATE/50:
        p.error("period must allow >=100 samples per level and all 32 steps within fixture")
    a.output.mkdir(parents=True, exist_ok=True)
    song = a.output / "envelope_0c_slow.ym"
    ours = a.output / "envelope_0c_slow.sauti.wav"
    reference = a.output / "envelope_0c_slow.stsound.wav"
    song.write_bytes(fixture(a.period))
    for binary, dest in ((a.sauti, ours), (a.stsound, reference)):
        subprocess.run([str(binary), str(song), str(dest)], check=True, stdout=subprocess.DEVNULL)
    rate_ours, pcm_ours = read_pcm(ours)
    rate_ref, pcm_ref = read_pcm(reference)
    if rate_ours != RATE or rate_ref != RATE:
        raise ValueError("unexpected PCM sample rates")
    ours_filtered = [0.5 * x for x in stsound_filter(pcm_ours)]
    print(f"YM 0C 32-level amplitude diagnostic: period={a.period}, approx {duration:.2f} samples/step")
    print("level | Sauti AC RMS | ST-Sound AC RMS | reference/Sauti")
    for level in range(32):
        # Central 30% of each step limits boundary contamination.
        start = round((level + .35)*duration)
        end = round((level + .65)*duration)
        if end > min(len(ours_filtered),len(pcm_ref)):
            raise ValueError("fixture shorter than expected envelope steps")
        v_ours = ac_rms(ours_filtered[start:end])
        v_ref = ac_rms(pcm_ref[start:end])
        ratio = v_ref/v_ours if v_ours > 1e-12 else float("nan")
        print(f" {level:2d}   | {v_ours:.8f} | {v_ref:.8f} | {ratio:.6f}")
    print("NOTE: windows indexed using Sauti's nominal clock. This is an AC-RMS")
    print("diagnostic, not direct DAC voltage measurement or proven ST-Sound level alignment.")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError) as exc:
        raise SystemExit(f"YM 32-step diagnostic failed: {exc}")
