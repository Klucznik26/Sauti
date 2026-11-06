#!/usr/bin/env python3
"""Generate deterministic AAC-LC mono test waveforms as signed 16-bit WAV.
No numpy, no third-party packages. Reference FFmpeg encodes these WAVs.
"""
import math
import os
import struct
import sys
import wave


def make_value(name, t, duration):
    if name == "tone440":
        return 0.32 * math.sin(2 * math.pi * 440 * t)
    if name == "tone1000":
        return 0.30 * math.sin(2 * math.pi * 1000 * t)
    if name == "multitone":
        return (0.21 * math.sin(2 * math.pi * 220 * t)
                + 0.17 * math.sin(2 * math.pi * 880 * t)
                + 0.08 * math.sin(2 * math.pi * 3000 * t))
    if name == "transient":
        # Alternating quiet and short bursts (forces spectral dynamics).
        period = t % 0.12
        envelope = 0.7 if period < 0.009 else (0.07 if period < 0.035 else 0)
        return envelope * math.sin(2 * math.pi * 1700 * t)
    if name == "chirp":
        # Increasing 150 -> 7000 Hz, continuous phase.
        f0, f1 = 150, 7000
        phase = 2 * math.pi * (f0 * t + (f1 - f0) * t * t / (2 * duration))
        return 0.29 * math.sin(phase)
    raise ValueError(name)


def main():
    if len(sys.argv) != 3:
        print("usage: generate_aac_fixtures.py output-directory sample-rate", file=sys.stderr)
        return 2
    directory, sample_rate_text = sys.argv[1:]
    rate = int(sample_rate_text)
    if rate not in (32000, 44100, 48000):
        raise ValueError("unsupported fixture sample rate")
    os.makedirs(directory, exist_ok=True)
    duration = 0.50
    count = int(duration * rate)
    for name in ("tone440", "tone1000", "multitone", "transient", "chirp"):
        path = os.path.join(directory, name + ".wav")
        with wave.open(path, "wb") as wav:
            wav.setnchannels(1)
            wav.setsampwidth(2)
            wav.setframerate(rate)
            block = bytearray()
            for index in range(count):
                value = max(-1.0, min(1.0, make_value(name, index / rate, duration)))
                block.extend(struct.pack("<h", round(value * 32767)))
            wav.writeframes(block)
        print("AAC fixture generated: %s (%d Hz)" % (path, rate))
    return 0


if __name__ == "__main__":
    sys.exit(main())
