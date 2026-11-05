#!/usr/bin/env python3
"""SBR-54w III: record metadata for a locally instrumented FFmpeg W export.
Use ONLY after an independent FFmpeg analysis path was actually executed
against the byte-identical core PCM. This script does not prove provenance.
"""
import argparse
import hashlib
import json
from pathlib import Path

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--core-rate",type=int,required=True)
    parser.add_argument("shared_core_pcm")
    parser.add_argument("ffmpeg_qmf_binary")
    parser.add_argument("output_manifest")
    args=parser.parse_args()
    try:
        pcm=Path(args.shared_core_pcm).read_bytes()
        if args.core_rate<=0 or len(pcm)<8192 or len(pcm)%4096:
            raise ValueError("invalid full-frame 1024-sample core PCM")
        frames=len(pcm)//4096
        if Path(args.ffmpeg_qmf_binary).stat().st_size!=frames*1024:
            raise ValueError("FFmpeg QMF output size mismatch; verify every frame")
        data=dict(format="sauti-sbr54w-low-qmf-complex32-v1",
                  producer="FFmpeg-instrumented-float-QMF",
                  independent=True,
                  core_sha256=hashlib.sha256(pcm).hexdigest(),
                  sample_rate_core=args.core_rate,
                  frame_count=frames,qmf_source="W[i-6]",
                  first_frame_preroll=6,bands=[2,3,4,5],
                  layout="frame-slot-band-re-im-f32le")
        Path(args.output_manifest).write_text(
            json.dumps(data,indent=2,sort_keys=True)+"\n",encoding="utf-8")
    except (OSError,ValueError) as exc:
        parser.exit(2,f"SBR-54w reference manifest rejected: {exc}\n")
    print(f"SBR-54w independent producer manifest: {frames} frames (declaration, not proof)")

if __name__=="__main__":
    main()
