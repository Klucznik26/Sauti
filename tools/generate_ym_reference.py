#!/usr/bin/env python3
"""Generate controlled YM3 PSG register tracks for Sauti/ST-Sound comparison."""
import argparse
from pathlib import Path

FRAMES = 150
TRACKS = ("tone_a", "tone_abc", "noise_a", "envelope_a", "tone_sweep", "tone_step_1s", "tone_step_2s", "tone_step_down_1s", "tone_step_down_2s", "envelope_09", "envelope_0a", "envelope_0c", "envelope_0e", "envelope_restart")


def create(name, envelope_period=200, fixed_volume=None):
    if fixed_volume is not None and not 0 <= fixed_volume <= 15:
        raise ValueError("fixed volume must be 0..15")
    if not 1 <= envelope_period <= 65535:
        raise ValueError('envelope period must be 1..65535')
    if name == "tone_fixed" and fixed_volume is None:
        raise ValueError("tone_fixed requires fixed_volume")
    regs = [bytearray(FRAMES) for _ in range(14)]
    for frame in range(FRAMES):
        regs[7][frame] = 0x3f  # All tone/noise sources disabled unless enabled below.
        regs[13][frame] = 0xff  # Do not restart envelope except when requested.
        if name == "tone_a" or name == "tone_fixed":
            regs[0][frame], regs[1][frame] = 250, 0
            regs[7][frame], regs[8][frame] = 0x3e, (15 if name == "tone_a" else fixed_volume)
        elif name == "tone_abc":
            for channel, period, volume in ((0, 250, 15), (1, 375, 12), (2, 500, 9)):
                regs[channel*2][frame] = period & 255
                regs[channel*2+1][frame] = period >> 8
                regs[8+channel][frame] = volume
            regs[7][frame] = 0x38
        elif name == "noise_a":
            regs[6][frame] = 8
            regs[7][frame], regs[8][frame] = 0x37, 15
        elif name == "envelope_a":
            regs[0][frame], regs[1][frame] = 250, 0
            regs[7][frame], regs[8][frame] = 0x3e, 0x10
            regs[11][frame], regs[12][frame] = envelope_period & 255, envelope_period >> 8
            if frame == 0:
                regs[13][frame] = 0x0e
        elif name.startswith("tone_step_"):
            switch = 50 if name.endswith("_1s") else 100
            falling = "down" in name
            period = (375 if frame < switch else 250) if falling else (250 if frame < switch else 375)
            regs[0][frame], regs[1][frame] = period & 255, period >> 8
            regs[7][frame], regs[8][frame] = 0x3e, 15
        elif name.startswith("envelope_"):
            regs[0][frame], regs[1][frame] = 250, 0
            regs[7][frame], regs[8][frame] = 0x3e, 0x10
            regs[11][frame], regs[12][frame] = envelope_period & 255, envelope_period >> 8
            if frame == 0 or (name == "envelope_restart" and frame == 50):
                regs[13][frame] = 0x0e if name == "envelope_restart" else int(name[-2:], 16)
        elif name == "tone_sweep":
            period = 100 + frame * 3
            regs[0][frame], regs[1][frame] = period & 255, period >> 8
            regs[7][frame], regs[8][frame] = 0x3e, 15
    return b"YM3!" + b"".join(regs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path, help="Output directory")
    args = parser.parse_args()
    args.directory.mkdir(parents=True, exist_ok=True)
    for name in TRACKS:
        output = args.directory / (name + ".ym")
        output.write_bytes(create(name))
        print(output)


if __name__ == "__main__":
    main()
