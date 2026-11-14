#!/usr/bin/env python3
"""SBR-54w: verify complete independent FFmpeg export before comparison.

Does not launch FFmpeg or infer provenance. It rejects missing/partial
output, nonfinite data and first-frame preroll contamination.
"""
import argparse
import array
from pathlib import Path
import sys

def validate(core, ffmpeg_qmf):
    pcm_bytes=Path(core).read_bytes()
    if len(pcm_bytes)<8192 or len(pcm_bytes)%4096:
        raise ValueError("core PCM must have >=2 full 1024-sample frames")
    n=len(pcm_bytes)//4096
    raw=Path(ffmpeg_qmf).read_bytes()
    if len(raw)!=n*1024:
        raise ValueError(f"QMF frame count mismatch: got {len(raw)} bytes, expected {n*1024}")
    a=array.array("f")
    a.frombytes(raw)
    if sys.byteorder!="little":
        a.byteswap()
    import math
    if not all(math.isfinite(v) for v in a):
        raise ValueError("nonfinite FFmpeg QMF")
    if any(v!=0 for v in a[:6*4*2]):
        raise ValueError("first frame preroll not zero")
    if not any(v!=0 for v in a[6*4*2:]):
        raise ValueError("QMF export silent or missing")
    return n

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("shared_core")
    p.add_argument("reference_qmf")
    args=p.parse_args()
    try:
        n=validate(args.shared_core,args.reference_qmf)
    except (OSError,ValueError) as exc:
        p.exit(2,f"SBR-54w FAIL: {exc}\n")
    print(f"SBR-54w independent QMF size/finite/preroll check PASS ({n} frames); provenance not verified")

if __name__=="__main__":
    main()
