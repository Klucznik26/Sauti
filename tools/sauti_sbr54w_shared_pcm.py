#!/usr/bin/env python3
"""SBR-54w: deterministic shared 1024-sample AAC-LC-core proxy PCM.

This is synthetic QMF *input*, not decoded AAC-LC PCM, and is fed
unchanged into BOTH Sauti and instrumented FFmpeg analysis paths.
"""
import argparse
import array
import math
import pathlib
import sys

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("output")
    p.add_argument("--frames",type=int,default=50)
    a=p.parse_args()
    if not 2<=a.frames<=1000:
        p.error("frames must be 2..1000")
    data=array.array("f")
    for i in range(a.frames*1024):
        t=i/24000.0
        # Three finite inharmonic/slowly varying tones; no randomness.
        v=(0.1*math.sin(2*math.pi*(490*t+80*t*t))
           +0.045*math.sin(2*math.pi*1210*t+0.31)
           +0.022*math.cos(2*math.pi*2910*t))
        data.append(v)
    if sys.byteorder!="little":
        data.byteswap()
    pathlib.Path(a.output).write_bytes(data.tobytes())
    print(f"SBR-54w shared synthetic PCM: {a.frames} frames, "
          f"{len(data)} float32 samples at 24 kHz (NOT AAC PCM)")

if __name__=="__main__":
    main()
