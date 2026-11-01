#!/usr/bin/env python3
"""AAC-29 deterministic, synthetic *stereo* audio fixtures (no copyrighted music).

Creates tonal, transient and dense-mix material at requested sample rate.
All signals are original/generated locally. WAV: stereo signed 16-bit PCM.
"""
import argparse
import math
import random
import struct
import wave
from pathlib import Path

DURATION = 0.52
TAU = 2.0 * math.pi


def synth(kind, rate):
    rng = random.Random(0xAAC29 + rate)
    n = round(rate * DURATION)
    output = bytearray()
    for i in range(n):
        t = i / rate
        # One fixed master beat (120 BPM), with instrument-like transients.
        beat = t % 0.25
        kick = math.exp(-48.0 * beat) * math.sin(
            TAU * (62 * beat + 100 * beat * math.exp(-25 * beat)))
        tick = (rng.random() * 2 - 1) * math.exp(-115 * beat)
        bass = math.sin(TAU * (110 * t + 0.45 * math.sin(TAU * 1.2 * t)))
        lead = math.sin(TAU * 440 * t) + 0.4 * math.sin(TAU * 660 * t)
        chord = (math.sin(TAU * 261.63 * t) +
                 0.72 * math.sin(TAU * 329.63 * t) +
                 0.65 * math.sin(TAU * 392.0 * t))
        sweep = math.sin(TAU * (250 * t + 2500 * t * t))
        if kind == "tonal":
            left = 0.24 * bass + 0.22 * lead + 0.08 * chord
            right = 0.17 * bass + 0.08 * lead + 0.23 * chord
        elif kind == "transient":
            left = 0.32 * kick + 0.18 * tick + 0.10 * sweep
            right = 0.23 * kick - 0.16 * tick + 0.13 * lead
        elif kind == "dense":
            left = (0.17 * bass + 0.17 * lead + 0.14 * chord +
                    0.12 * kick + 0.06 * tick)
            right = (0.12 * bass + 0.08 * lead + 0.21 * chord +
                     0.08 * kick - 0.07 * tick + 0.04 * sweep)
        else:
            raise ValueError(kind)
        # Keep level away from clipping and avoid the false positive of silence.
        left = max(-0.95, min(0.95, left))
        right = max(-0.95, min(0.95, right))
        output.extend(struct.pack("<hh", round(left * 32767),
                                  round(right * 32767)))
    return output


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("directory", type=Path)
    ap.add_argument("rate", type=int, choices=(32000, 44100, 48000))
    args = ap.parse_args()
    args.directory.mkdir(parents=True, exist_ok=True)
    for kind in ("tonal", "transient", "dense"):
        path = args.directory / (kind + ".wav")
        with wave.open(str(path), "wb") as dst:
            dst.setnchannels(2)
            dst.setsampwidth(2)
            dst.setframerate(args.rate)
            dst.writeframes(synth(kind, args.rate))
        print("AAC-29 fixture:", path, args.rate, "Hz")


if __name__ == "__main__":
    main()
