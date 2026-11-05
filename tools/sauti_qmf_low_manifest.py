#!/usr/bin/env python3
"""Prepare a deterministic SHA256-bound Sauti LOW-QMF export manifest.

The input is raw float32 little-endian mono core PCM with exactly
1024 samples per AAC-LC frame. This intentionally DOES NOT decode ADTS.
The same byte-exact input must be used to instrument independent
reference analysis, otherwise numerical QMF agreement is not testable.
"""
import argparse
import hashlib
import json
from pathlib import Path

FORMAT = "sauti-sbr54w-low-qmf-complex32-v1"

def make_manifest(core: Path, qmf: Path, rate: int):
    if rate <= 0:
        raise ValueError("invalid core sample rate")
    content = core.read_bytes()
    if len(content) < 2*1024*4 or len(content)%(1024*4):
        raise ValueError("expected at least two complete PCM core frames")
    frames=len(content)//(1024*4)
    if qmf.stat().st_size != frames*32*4*2*4:
        raise ValueError("QMF export size does not match PCM frame count")
    return dict(format=FORMAT, producer="Sauti", independent=False,
                core_sha256=hashlib.sha256(content).hexdigest(),
                sample_rate_core=rate, frame_count=frames,
                qmf_source="W[i-6]", first_frame_preroll=6,
                bands=[2,3,4,5], layout="frame-slot-band-re-im-f32le")

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--core-rate",type=int,required=True)
    parser.add_argument("core_pcm")
    parser.add_argument("qmf_bin")
    parser.add_argument("manifest_path")
    args=parser.parse_args()
    core,qmf,dst=map(Path,(args.core_pcm,args.qmf_bin,args.manifest_path))
    try:
        value=make_manifest(core,qmf,args.core_rate)
        dst.write_text(json.dumps(value,indent=2,sort_keys=True)+"\n",
                       encoding="utf-8")
    except (ValueError,OSError) as error:
        parser.exit(2,f"SBR-54w manifest rejected: {error}\n")
    print(f"SBR-54w manifest written: {dst} frames={value['frame_count']}")


if __name__=="__main__":
    main()
