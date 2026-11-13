#!/usr/bin/env python3
"""Generate reproducible stereo HF-rich audio, encode HE-AAC v2 with
Fraunhofer FDK and run independent Sauti/FFmpeg comparisons.

Requires FFmpeg with libfdk_aac encoder and numpy for compare helper.
Reference decoding is done by tools/sauti_ps5_compare.py; the Sauti
decoder itself has no dependency on FFmpeg.
"""
import argparse
import math
import random
import shutil
import struct
import subprocess
import sys
import wave
from pathlib import Path

RATE = 48000


def fixture(path: Path, seconds: int, seed: int = 260113) -> None:
    """Independent channels: LF anchor, HF partials, chirp and light noise."""
    rng = random.Random(seed)
    partials_l = [(330, .06), (700, .025), (1900, .04),
                  (3500, .026), (6100, .028), (9400, .022), (13500, .018)]
    partials_r = [(570, .05), (1250, .035), (2700, .036),
                  (4600, .025), (7800, .026), (11000, .020), (15000, .015)]
    # Fast generation via recursive oscillator avoids millions of math.sin calls.
    def oscillators(tones):
        return [[0.0, 1.0, math.sin(2*math.pi*f/RATE),
                 math.cos(2*math.pi*f/RATE), amp] for f, amp in tones]
    l_osc, r_osc = oscillators(partials_l), oscillators(partials_r)
    n = RATE * seconds
    with wave.open(str(path), "wb") as out:
        out.setnchannels(2)
        out.setsampwidth(2)
        out.setframerate(RATE)
        buffer = bytearray()
        for i in range(n):
            t = i / RATE
            # Amplitude envelope ramps prevent an accidental sharp start.
            ramp = min(1.0, i / (RATE * .10), (n-i-1)/(RATE*.10))
            left = right = 0.0
            for osc in l_osc:
                left += osc[0] * osc[4]
                sn, cs = osc[0], osc[1]
                osc[0] = sn*osc[3]+cs*osc[2]
                osc[1] = cs*osc[3]-sn*osc[2]
            for osc in r_osc:
                right += osc[0] * osc[4]
                sn, cs = osc[0], osc[1]
                osc[0] = sn*osc[3]+cs*osc[2]
                osc[1] = cs*osc[3]-sn*osc[2]
            # Two independent continuous linear chirps: 3..11 and 5..14 kHz.
            left += .035*math.sin(2*math.pi*(3000*t+4000*t*t/seconds))
            right += .035*math.sin(2*math.pi*(5000*t+4500*t*t/seconds))
            # Fixed-seed broadband component ensures continuous HF energy.
            left += .010*(2*rng.random()-1)
            right += .010*(2*rng.random()-1)
            left = max(-.95, min(.95, ramp*left))
            right = max(-.95, min(.95, ramp*right))
            buffer += struct.pack("<hh", round(left*32767), round(right*32767))
            if len(buffer) >= 131072:
                out.writeframesraw(buffer)
                buffer.clear()
        if buffer:
            out.writeframesraw(buffer)


def run(command):
    print("+", " ".join(map(str, command)), flush=True)
    subprocess.run(list(map(str, command)), check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out-dir", type=Path,
                        default=Path("/tmp/sauti_ps_broadband"))
    parser.add_argument("--seconds", type=int, default=10)
    parser.add_argument("--bitrate", default="32k",
                        help="FDK HE-AAC v2 stereo at 48kHz: 18k..56k; default 32k")
    parser.add_argument("--generate-only", action="store_true",
                        help="generate WAV and encoded AAC without running comparisons")
    args = parser.parse_args()
    if args.seconds < 3 or args.seconds > 60:
        parser.error("--seconds must be in 3..60")
    try:
        rate = args.bitrate.lower().strip()
        bits = int(rate[:-1]) * 1000 if rate.endswith("k") else int(rate)
    except ValueError:
        parser.error("--bitrate must be an integer in bit/s or e.g. 32k")
    if not 18000 <= bits <= 56000:
        parser.error("FDK HE-AAC v2 stereo at 48kHz requires bitrate 18k..56k; try 32k")
    if not shutil.which("ffmpeg"):
        parser.error("FFmpeg not found")
    encoders = subprocess.run(["ffmpeg", "-hide_banner", "-encoders"],
                             capture_output=True, text=True, check=False)
    if encoders.returncode or "libfdk_aac" not in encoders.stdout:
        parser.error("FFmpeg build needs a libfdk_aac ENCODER (decoder alone is insufficient)")
    directory = args.out_dir
    directory.mkdir(parents=True, exist_ok=True)
    wav = directory / "broadband_source.wav"
    aac = directory / "broadband_he_v2.aac"
    wav.unlink(missing_ok=True)
    aac.unlink(missing_ok=True)
    fixture(wav, args.seconds)
    run(["ffmpeg", "-hide_banner", "-nostdin", "-loglevel", "error", "-y",
         "-i", wav, "-c:a", "libfdk_aac", "-profile:a", "aac_he_v2",
         "-b:a", args.bitrate, "-f", "adts", aac])
    print("Fixture:", aac, "bytes:", aac.stat().st_size, flush=True)
    # Sanity check both the coding format and reference decode.
    run(["ffmpeg", "-hide_banner", "-nostdin", "-loglevel", "error",
         "-c:a", "libfdk_aac", "-i", aac, "-f", "null", "-"])
    if not args.generate_only:
        run([sys.executable, Path(__file__).with_name("sauti_ps_isolate.py"),
             aac, "--out-dir", directory / "decoded"])
    print("Broadband fixture complete. This measures real HF information "
          "and does NOT imply a conformance PASS.", flush=True)


if __name__ == "__main__":
    try:
        main()
    except (OSError, subprocess.CalledProcessError) as exc:
        print("FAIL broadband fixture:", exc, file=sys.stderr)
        sys.exit(1)
