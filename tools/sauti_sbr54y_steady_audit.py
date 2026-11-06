#!/usr/bin/env python3
"""SBR-54y: independent complex QMF startup versus steady-state audit.

Read EXISTING SBR-54x tone exports (no additional FFmpeg decoding).
Use fixed absolute 32-sample-slot windows across each 50-frame trace:
  early:      slots   6..63
  transition: slots  64..255
  steady:     slots 256..end
Report band-specific complex coherence, optimal complex gain residual,
FFmpeg/Sauti RMS ratio and cross-band energy share within LOW 2..5.
Each window is fit independently; per-band energy shares across slots
DO NOT represent the entire 32-band QMF spectrum.
"""
import argparse
import csv
import math
from pathlib import Path

from sauti_he_aac_qmf_reference_audit import load_manifest, read_complex

BANDS=(2,3,4,5)
WINDOWS=(("early",6,64),("transition",64,256),("steady",256,None))


def analyze_window(sa,ff,frames,start,stop):
    stop = min(frames*32,stop if stop is not None else frames*32)
    if start>=stop:
        raise ValueError(f"empty window {start}:{stop}")
    band_data=[]
    for b in range(4):
        x=[complex(sa[(s*4+b)*2],sa[(s*4+b)*2+1])
           for s in range(start,stop)]
        y=[complex(ff[(s*4+b)*2],ff[(s*4+b)*2+1])
           for s in range(start,stop)]
        ex=math.fsum(abs(v)**2 for v in x)
        ey=math.fsum(abs(v)**2 for v in y)
        if ex<=0 or ey<=0:
            raise ValueError(f"LOW {b+2}: zero-energy window {start}:{stop}")
        dot=sum((v.conjugate()*w for v,w in zip(x,y)),0j)
        coh=min(1.0,abs(dot)/math.sqrt(ex*ey))
        residual=math.sqrt(max(0.0,1.0-coh*coh))
        band_data.append({"band":b+2,"coherence":coh,
                          "residual":residual,"ff_sauti_rms":math.sqrt(ey/ex),
                          "sa_energy":ex,"ff_energy":ey})
    sum_sa=math.fsum(v["sa_energy"] for v in band_data)
    sum_ff=math.fsum(v["ff_energy"] for v in band_data)
    for value in band_data:
        value["sa_share"]=value["sa_energy"]/sum_sa
        value["ff_share"]=value["ff_energy"]/sum_ff
        value["n"]=stop-start
    return band_data


def check_and_load(folder):
    sa_m=load_manifest(folder/"sauti.json")
    ff_m=load_manifest(folder/"ffmpeg.json")
    if sa_m["independent"] or not ff_m["independent"] or sa_m["producer"]==ff_m["producer"]:
        raise ValueError(f"{folder}: missing independent reference")
    for field in ("core_sha256","sample_rate_core","frame_count","qmf_source",
                  "first_frame_preroll","bands","layout"):
        if sa_m[field]!=ff_m[field]:
            raise ValueError(f"{folder}: manifest mismatch {field}")
    if sa_m["frame_count"]<9:
        raise ValueError(f"{folder}: at least 9 frames for steady-state windows")
    return (read_complex(folder/"sauti.qmf.f32le",sa_m["frame_count"]),
            read_complex(folder/"ffmpeg.qmf.f32le",sa_m["frame_count"]),
            sa_m["frame_count"])


def report(directory):
    rows=[]
    for target in BANDS:
        folder=directory/f"low{target}"
        sa,ff,frames=check_and_load(folder)
        for name,start,stop in WINDOWS:
            values=analyze_window(sa,ff,frames,start,stop)
            for value in values:
                row=dict(excite_low=target,window=name,from_slot=start,
                         to_slot=min(frames*32,stop if stop is not None else frames*32),
                         **value)
                rows.append(row)
                if value["band"]==target:
                    print(f"EXCITE LOW={target} {name:10s} n={value['n']:4d} "
                          f"coherence={value['coherence']:.8f} "
                          f"resid_complex={value['residual']:.8f} "
                          f"FF/Sauti_RMS={value['ff_sauti_rms']:.5f} "
                          f"main_share_Sauti={value['sa_share']:.8f} "
                          f"main_share_FF={value['ff_share']:.8f}",flush=True)
    path=directory/"steady_report.tsv"
    fields=("excite_low","window","from_slot","to_slot","band","n",
            "coherence","residual","ff_sauti_rms",
            "sa_share","ff_share","sa_energy","ff_energy")
    with path.open("w",encoding="utf-8",newline="") as f:
        writer=csv.DictWriter(f,delimiter="\t",fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)
    print(f"SBR-54y: 4 tones x 3 windows x 4 observed bands, report={path}")
    print("DIAGNOSTIC only: no production-QMF correction or AAC conformance claim.")


def self_test():
    import array
    frames=10
    sa=array.array("f",[0.0]*(frames*32*4*2))
    ff=array.array("f",[0.0]*(frames*32*4*2))
    for slot in range(frames*32):
        for band in range(4):
            idx=(slot*4+band)*2
            val=math.sin(slot*0.2+band)+2.0
            sa[idx]=val
            ff[idx]=val*32768
            sa[idx+1]=val*0.3
            ff[idx+1]=val*0.3*32768
    for name,start,end in WINDOWS:
        v=analyze_window(sa,ff,frames,start,end)
        assert len(v)==4 and all(x["n"]>0 for x in v)
        assert all(x["coherence"]>0.999999 and x["residual"]<0.00001 for x in v)
        assert all(abs(x["ff_sauti_rms"]/32768-1)<1e-6 for x in v)
    try:
        analyze_window(sa,ff,frames,frames*32,frames*32)
    except ValueError:
        pass
    else:
        raise AssertionError("empty window accepted")
    print("PASS SBR-54y startup/steady QMF window audit (synthetic only)")


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--input-dir",default="/tmp/sbr54x-tones")
    p.add_argument("--self-test",action="store_true")
    args=p.parse_args()
    if args.self_test:
        self_test()
    else:
        report(Path(args.input_dir))


if __name__=="__main__":
    try:
        main()
    except (OSError,ValueError) as exc:
        raise SystemExit(f"SBR-54y rejected: {exc}")
