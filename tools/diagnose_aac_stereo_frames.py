#!/usr/bin/env python3
"""AAC-29: locate stereo PCM error by frame, channel and sample offset.

Pure diagnostic, not a conformance gate. The first overlap-warmup frame
is omitted, matching the strict comparator's aggregate sample range.
"""
import argparse
import math
from pathlib import Path
import sys

sys.path.insert(0,str(Path(__file__).resolve().parent))
from compare_aac_pcm import FRAME, load_pcm, load_mask


def inspect(native, reference, rows, top=8):
    if not rows or len(native)!=len(rows)*FRAME*2 or len(reference)!=len(native):
        raise ValueError("frame count or PCM length mismatch")
    results=[]
    for frame in range(1,len(rows)):
        if rows[frame][0] != 1 or rows[frame-1][0] != 1:
            continue
        for ch,name in enumerate(("LEFT","RIGHT")):
            sum_r=sum_delta=0.0
            max_err=0.0
            max_offset=0
            for i in range(FRAME):
                idx=2*(frame*FRAME+i)+ch
                n,r=float(native[idx]),float(reference[idx])
                if not (math.isfinite(n) and math.isfinite(r)):
                    raise ValueError("nonfinite PCM frame %d offset %d channel %s" %
                                     (frame,i,name))
                delta=n-r
                sum_r+=r*r
                sum_delta+=delta*delta
                if abs(delta)>max_err:
                    max_err=abs(delta)
                    max_offset=i
            rmse=math.sqrt(sum_delta/FRAME)
            ref_rms=math.sqrt(sum_r/FRAME)
            results.append({"frame":frame,"channel":name,
                            "nrmse":rmse/ref_rms if ref_rms>1e-15 else None,
                            "rmse":rmse,"ref_rms":ref_rms,
                            "max_error":max_err,"peak_offset":max_offset})
    ranked=sorted(results,key=lambda x:x["rmse"],reverse=True)
    return ranked[:top],results


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("native")
    p.add_argument("reference")
    p.add_argument("mask")
    p.add_argument("--top",type=int,default=8)
    args=p.parse_args()
    try:
        ranked,all_frames=inspect(load_pcm(args.native),load_pcm(args.reference),
                                  load_mask(args.mask),args.top)
    except (OSError,ValueError) as exc:
        print("FAIL AAC-29 frame diagnostic:",exc,file=sys.stderr)
        return 1
    print("AAC-29 per-frame diagnostics; comparable channel frames:",len(all_frames))
    for row in ranked:
        norm="n/a" if row["nrmse"] is None else "%.5f"%row["nrmse"]
        print("frame=%d channel=%s RMSE=%.7g nRMSE=%s max=%.7g peak_sample=%d" %
              (row["frame"],row["channel"],row["rmse"],norm,
               row["max_error"],row["peak_offset"]))
    return 0


if __name__=="__main__":
    sys.exit(main())
