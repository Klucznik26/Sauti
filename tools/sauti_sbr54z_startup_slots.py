#!/usr/bin/env python3
"""SBR-54z: slot-by-slot startup QMF audit against a steady-state complex gain.

Uses existing SBR-54x mono tone exports and strict matching SHA manifests.
Fit one complex FFmpeg->Sauti gain per stimulated band over global
slots 256..end. Inspect slots 6..63 individually relative to that FIXED
gain, without hiding transient errors by independently fitting each slot.
Single complex samples have NO meaningful magnitude coherence statistic;
report amplitude ratio, relative phase and normalized complex residual.
Small/reference-zero amplitudes are explicitly flagged, not divided by 0.
"""
import argparse
import csv
import math
from pathlib import Path
from sauti_sbr54y_steady_audit import BANDS, check_and_load

FIRST_VALID=6
LAST_EARLY=64
STEADY_START=256
GROUPS=(("startup_6_15",6,16),("startup_16_31",16,32),
        ("startup_32_47",32,48),("startup_48_63",48,64))


def get(data,slot,band):
    i=(slot*4+(band-2))*2
    return complex(data[i],data[i+1])


def fit_gain(sa,ref,band,frames):
    if frames*32<=STEADY_START:
        raise ValueError("insufficient samples to fit steady-state gain")
    ex=0.0
    xy=0j
    for slot in range(STEADY_START,frames*32):
        s=get(sa,slot,band)
        f=get(ref,slot,band)
        ex+=abs(f)**2
        xy+=f.conjugate()*s
    if ex<=0 or not math.isfinite(ex):
        raise ValueError("zero or invalid reference energy in steady-state")
    gain=xy/ex
    if not math.isfinite(gain.real) or not math.isfinite(gain.imag) or abs(gain)==0:
        raise ValueError("nonfinite or zero steady-state gain")
    return gain


def measure(sa,ref,band,frames):
    gain=fit_gain(sa,ref,band,frames)
    steady_energy=sum(abs(get(sa,t,band))**2 for t in range(STEADY_START,frames*32))
    steady_rms=math.sqrt(steady_energy/(frames*32-STEADY_START))
    floor=max(steady_rms*1e-5,1e-20)
    rows=[]
    for t in range(FIRST_VALID,LAST_EARLY):
        x=get(sa,t,band)
        scaled=gain*get(ref,t,band)
        denom=abs(scaled)
        comparable=denom>=floor and abs(x)>=floor
        row=dict(slot=t,sa_magnitude=abs(x),scaled_ref_magnitude=denom,
                 magnitude_ratio=(abs(x)/denom if comparable else None),
                 phase_delta_deg=(math.degrees(math.atan2((x/scaled).imag,(x/scaled).real))
                                  if comparable else None),
                 relative_complex_error=(abs(x-scaled)/denom if comparable else None),
                 low_energy=(not comparable))
        rows.append(row)
    return gain,rows


def summary(rows):
    for label,start,stop in GROUPS:
        values=[x["relative_complex_error"] for x in rows
                if start<=x["slot"]<stop and not x["low_energy"]]
        if values:
            print(f"  {label} usable={len(values)}/{stop-start} "
                  f"median_relative_error={sorted(values)[len(values)//2]:.7g} "
                  f"max_relative_error={max(values):.7g}")
        else:
            print(f"  {label} usable=0/{stop-start} (low energy)")


def report(root):
    all_rows=[]
    for tone in BANDS:
        sa,ref,frames=check_and_load(root/f"low{tone}")
        if frames*32<LAST_EARLY:
            raise ValueError("need at least 64 global slots")
        gain,rows=measure(sa,ref,tone,frames)
        print(f"EXCITE LOW={tone} steady_gain=(Re={gain.real:.10g}, Im={gain.imag:.10g}) "
              f"startup slots=6..63")
        summary(rows)
        for row in rows:
            all_rows.append(dict(excite_low=tone,**row))
    result=root/"startup_slots_report.tsv"
    fields=("excite_low","slot","sa_magnitude","scaled_ref_magnitude",
            "magnitude_ratio","phase_delta_deg","relative_complex_error","low_energy")
    with result.open("w",newline="",encoding="utf-8") as f:
        writer=csv.DictWriter(f,delimiter="\t",fieldnames=fields)
        writer.writeheader()
        writer.writerows(all_rows)
    print(f"SBR-54z: 4 x 58 startup slots, report={result}")
    print("DIAGNOSTIC ONLY: no one-sample coherence, no production changes.")


def self_test():
    import array
    n=10*32*4*2
    sa=array.array("f",[0.0]*n)
    ff=array.array("f",[0.0]*n)
    for slot in range(320):
        for b in range(4):
            i=(slot*4+b)*2
            ff[i]=float(32768*(1+b))
            sa[i]=float(1+b)
            if slot<16:
                sa[i]*=0.75
    gain,rows=measure(sa,ff,2,10)
    assert abs(gain.real-1/32768)<1e-12 and abs(gain.imag)<1e-12
    assert len(rows)==58 and abs(rows[0]["relative_complex_error"]-.25)<1e-6
    assert abs(rows[-1]["relative_complex_error"])<1e-6
    assert all(not row["low_energy"] for row in rows)
    for slot in range(6,64):
        ff[(slot*4+0)*2]=0
    _,rows=measure(sa,ff,2,10)
    assert all(row["low_energy"] for row in rows)
    assert all(row["relative_complex_error"] is None for row in rows)
    print("PASS SBR-54z startup slot audit (synthetic: fixed gain, transient, zero reference)")


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input-dir",default="/tmp/sbr54x-tones")
    parser.add_argument("--self-test",action="store_true")
    args=parser.parse_args()
    if args.self_test:
        self_test()
    else:
        report(Path(args.input_dir))


if __name__=="__main__":
    try:
        main()
    except (OSError,ValueError) as exc:
        raise SystemExit(f"SBR-54z rejected: {exc}")
