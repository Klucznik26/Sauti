#!/usr/bin/env python3
"""SBR-54w: diagnostic gain/phase calibration of independent complex QMF.

Uses existing strict manifest/data validation. Independent per-band fit is
DIAGNOSTIC, not a decoder correction or proof of conformant phase.
For each band: best real gain g minimizing ||g*ref - sauti||² and best
complex gain h minimizing ||h*ref - sauti||². Report residuals relative
to Sauti and magnitude-only coherence; flag low-energy bands.
"""
import argparse
import math
import sys
from sauti_he_aac_qmf_reference_audit import load_manifest, read_complex

def evaluate(sa, ref, frames):
    results=[]
    for band in range(4):
        xs=[];ys=[]
        for frame in range(frames):
            for slot in range(6 if frame == 0 else 0,32):
                i=((frame*32+slot)*4+band)*2
                xs.append(complex(sa[i],sa[i+1]))
                ys.append(complex(ref[i],ref[i+1]))
        e_x=math.fsum(abs(v)**2 for v in xs)
        e_y=math.fsum(abs(v)**2 for v in ys)
        if e_x==0 or e_y==0:
            raise ValueError(f"LOW {band+2}: zero-energy band")
        dot=sum((y.conjugate()*x for x,y in zip(xs,ys)),0j)
        g=dot.real/e_y
        h=dot/e_y
        err_real=math.fsum(abs(x-g*y)**2 for x,y in zip(xs,ys))
        err_complex=math.fsum(abs(x-h*y)**2 for x,y in zip(xs,ys))
        results.append((band+2,len(xs), math.sqrt(e_y/e_x),g,h,
                        math.sqrt(err_real/e_x),
                        math.sqrt(err_complex/e_x),
                        abs(dot)/math.sqrt(e_x*e_y),
                        e_x/e_y))
    return results

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("sauti_qmf")
    p.add_argument("sauti_manifest")
    p.add_argument("ffmpeg_qmf")
    p.add_argument("ffmpeg_manifest")
    args=p.parse_args()
    try:
        a=load_manifest(args.sauti_manifest)
        b=load_manifest(args.ffmpeg_manifest)
        if a["independent"] or not b["independent"] or a["producer"]==b["producer"]:
            raise ValueError("independent FFmpeg producer required")
        for k in ("core_sha256","sample_rate_core","frame_count","qmf_source",
                  "first_frame_preroll","bands","layout"):
            if a[k]!=b[k]:
                raise ValueError(f"manifest mismatch: {k}")
        sa=read_complex(args.sauti_qmf,a["frame_count"])
        ff=read_complex(args.ffmpeg_qmf,a["frame_count"])
        print("SBR-54w: diagnostic per-band gain/phase fit; NOT a decoder correction")
        for band,n,raw_ratio,g,h,rerr,cerr,coh,eratio in evaluate(sa,ff,a["frame_count"]):
            print(f"LOW={band} n={n} FF/Sauti_rms={raw_ratio:.9g} "
                  f"real_gain(FF->Sauti)={g:.9g} "
                  f"complex_gain=({h.real:.9g},{h.imag:.9g}) "
                  f"resid_real/Sauti={rerr:.9g} resid_complex/Sauti={cerr:.9g} "
                  f"abs_coherence={coh:.9g} sa/ref_energy={eratio:.9g}")
    except (OSError,ValueError) as exc:
        p.exit(2,f"SBR-54w gain audit rejected: {exc}\n")
if __name__=="__main__":
    main()
