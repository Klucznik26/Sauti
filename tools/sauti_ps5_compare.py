#!/usr/bin/env python3
"""PS-5a: independent FFmpeg reference comparison for Sauti HE-AAC v2.
Only the REFERENCE uses FFmpeg; Sauti native decoder runs separately.
Output is diagnostic, not a normative conformance certificate.
Requires numpy and ffmpeg in PATH. """
import argparse
import math
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

def run(cmd):
    p = subprocess.run(cmd, capture_output=True, text=True, check=False)
    if p.returncode:
        raise RuntimeError(f"{' '.join(map(str,cmd))}: exit={p.returncode}\\n{p.stderr[-3500:]}")
    return p

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("input",type=Path,help="independently encoded ADTS HE-AAC v2")
    ap.add_argument("--native",default="./build/sauti_he_ps_decode")
    ap.add_argument("--rate",type=int,default=48000,
                    help="HE-AAC output sample rate (default: 48000)")
    ap.add_argument("--search",type=int,default=8192,
                    help="maximum independent alignment lag in stereo sample pairs")
    a=ap.parse_args()
    if a.rate<=0 or a.search<0:ap.error("invalid rate/search")
    if not a.input.is_file():ap.error("input ADTS file missing")
    if not shutil.which("ffmpeg"):ap.error("ffmpeg executable missing")
    try:
        import numpy as np
    except ImportError:
        ap.error("Install python3-numpy to compute the comparison")

    with tempfile.TemporaryDirectory(prefix="sauti_ps5_") as tmp:
        native=Path(tmp)/"sauti.f32le"
        ref=Path(tmp)/"ffmpeg.f32le"
        decoder=run([a.native,str(a.input),str(native)])
        reference=run(["ffmpeg","-hide_banner","-nostdin","-loglevel","error",
                       "-i",str(a.input),"-map","0:a:0",
                       "-ac","2","-ar",str(a.rate),"-f","f32le",
                       "-c:a","pcm_f32le",str(ref),"-y"])
        native_pcm=np.fromfile(native,dtype="<f4")
        reference_pcm=np.fromfile(ref,dtype="<f4")
        if native_pcm.size%2 or reference_pcm.size%2 or not native_pcm.size or not reference_pcm.size:
            raise RuntimeError("empty or non-stereo PCM output")
        x=native_pcm.reshape((-1,2)).astype(np.float64)
        y=reference_pcm.reshape((-1,2)).astype(np.float64)
        if not np.isfinite(x).all() or not np.isfinite(y).all():
            raise RuntimeError("NaN or infinity in PCM")
        # Search over realistic decoder priming delays. Lag is relative
        # reference index for native[0]. Correlation removes gain
        # differences; the final metrics retain the original gain.
        def segment(lag):
            start_x=max(0,-lag)
            start_y=max(0,lag)
            n=min(x.shape[0]-start_x,y.shape[0]-start_y)
            return start_x,start_y,n
        def corr(lag):
            sx,sy,n=segment(lag)
            if n<1024:return float("-inf")
            # Fixed-size window avoids preferring smaller overlaps.
            n=min(n,8192)
            a1=x[sx:sx+n].reshape(-1)
            b1=y[sy:sy+n].reshape(-1)
            den=np.linalg.norm(a1)*np.linalg.norm(b1)
            if den<1e-12:return float("-inf")
            return float(np.dot(a1,b1)/den)
        coarse=range(-a.search,a.search+1,64)
        center=max(coarse,key=corr)
        best=max(range(max(-a.search,center-63),min(a.search,center+63)+1),key=corr)
        sx,sy,n=segment(best)
        if n<1024:raise RuntimeError("Not enough aligned stereo output")
        aa=x[sx:sx+n];bb=y[sy:sy+n]
        for name,audio in (("native",aa),("ffmpeg",bb)):
            rms=np.sqrt(np.mean(audio*audio,axis=0))
            peak=np.max(np.abs(audio),axis=0)
            print(f"{name}: frames={audio.shape[0]} RMS_L/R={rms[0]:.7g}/{rms[1]:.7g} "
                  f"peak_L/R={peak[0]:.7g}/{peak[1]:.7g}")
        diff=aa-bb
        err=np.sqrt(np.mean(diff*diff,axis=0))
        channel_corr=[]
        for k in (0,1):
            u=aa[:,k]-np.mean(aa[:,k])
            v=bb[:,k]-np.mean(bb[:,k])
            denom=np.linalg.norm(u)*np.linalg.norm(v)
            channel_corr.append(float(np.dot(u,v)/denom) if denom>1e-12 else float("nan"))
        print(f"alignment_lag={best} pairs ({1000*best/a.rate:.3f} ms), "
              f"global_corr={corr(best):.6f}, aligned_pairs={n}")
        print(f"RMSE_L/R={err[0]:.7g}/{err[1]:.7g}, "
              f"channel_corr_L/R={channel_corr[0]:.6f}/{channel_corr[1]:.6f}")
        print(f"native_pairs={len(x)} ref_pairs={len(y)} "
              f"(native intentionally retains final pending 2048 pairs)")
        print("DIAGNOSTIC ONLY — check fidelity and SBR/PS timing; this is not a compliance PASS")
        print(decoder.stderr.strip())
    return 0

if __name__=="__main__":
    try:sys.exit(main())
    except (RuntimeError,OSError) as exc:
        print(f"FAIL PS-5a: {exc}",file=sys.stderr)
        sys.exit(1)
