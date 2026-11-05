#!/usr/bin/env python3
"""SBR-54w: diagnostic inter-analyzer QMF slot lag sweep.

No decoder changes. Test integer lag of reference versus Sauti using
normalized complex correlation. For lag L, compare Sauti[t] with FFmpeg[t+L],
with first-frame six-slot preroll excluded in BOTH streams. Assess coherence
and the best complex-gain residual for every LOW band and lag.
"""
import argparse
import math
import sys
from sauti_he_aac_qmf_reference_audit import load_manifest, read_complex

def series(data,frames,band):
    return [complex(data[((frame*32+slot)*4+band)*2],
                    data[((frame*32+slot)*4+band)*2+1])
            for frame in range(frames) for slot in range(32)]

def lag_stats(a,b,lag):
    start=max(6,6-lag)
    stop=min(len(a),len(b)-lag)
    if stop<=start:
        raise ValueError("no overlapping valid slots")
    xx=a[start:stop]
    yy=b[start+lag:stop+lag]
    ea=math.fsum(abs(z)**2 for z in xx)
    eb=math.fsum(abs(z)**2 for z in yy)
    if ea<=0 or eb<=0:
        return None
    dot=sum((x.conjugate()*y for x,y in zip(xx,yy)),0j)
    coh=abs(dot)/math.sqrt(ea*eb)
    # Best arbitrary complex scaling of FFmpeg -> Sauti: residual sqrt(1-coh²)
    resid=math.sqrt(max(0,1-coh*coh))
    return (coh,resid,ea,eb,len(xx))

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("sauti_qmf")
    p.add_argument("sauti_manifest")
    p.add_argument("ffmpeg_qmf")
    p.add_argument("ffmpeg_manifest")
    p.add_argument("--max-lag",type=int,default=16)
    args=p.parse_args()
    try:
        if not 0<=args.max_lag<=64:
            raise ValueError("max-lag must be 0..64")
        a=load_manifest(args.sauti_manifest)
        b=load_manifest(args.ffmpeg_manifest)
        if a["independent"] or not b["independent"] or a["producer"]==b["producer"]:
            raise ValueError("independent FFmpeg reference producer required")
        for k in ("core_sha256","sample_rate_core","frame_count","qmf_source",
                  "first_frame_preroll","bands","layout"):
            if a[k]!=b[k]:
                raise ValueError("manifest mismatch: "+k)
        sa=read_complex(args.sauti_qmf,a["frame_count"])
        ff=read_complex(args.ffmpeg_qmf,a["frame_count"])
        print("SBR-54w: integer QMF slot lag sweep; lag=ref slot minus Sauti slot")
        for band in range(4):
            x=series(sa,a["frame_count"],band)
            y=series(ff,a["frame_count"],band)
            ranked=[]
            for lag in range(-args.max_lag,args.max_lag+1):
                r=lag_stats(x,y,lag)
                if r is not None:
                    ranked.append((r[0],lag,r))
            ranked.sort(reverse=True)
            zero=next((v for v in ranked if v[1]==0),None)
            if zero is None:
                raise ValueError("no zero-lag overlap")
            best=ranked[0]
            print(f"LOW={band+2} zero_coherence={zero[0]:.9g} "
                  f"best_lag={best[1]:+d} best_coherence={best[0]:.9g} "
                  f"best_complex_residual={best[2][1]:.9g} "
                  f"n={best[2][4]} "
                  f"top_lags={','.join(f'{lag:+d}:{coh:.5f}' for coh,lag,_ in ranked[:3])}")
    except (OSError,ValueError) as exc:
        p.exit(2,f"SBR-54w lag audit rejected: {exc}\n")

if __name__=="__main__":
    main()
