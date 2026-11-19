#!/usr/bin/env python3
"""SBR-99: per-channel comparison of two float32 interleaved decodes of the same stream.

usage: sauti_multichannel_compare.py REF.f32 OURS.f32 CHANNELS [MIN_DB=100]
Every channel of OURS must equal the SAME-index channel of REF (identity order, not a best match), so a swapped side/back pair or a wrong LFE position fails.
Prints per-channel SNR in dB; exit 0 when all reach MIN_DB, 1 otherwise, 2 when the lengths differ."""
import sys
import numpy as np
ch=int(sys.argv[3]); lo=float(sys.argv[4]) if len(sys.argv)>4 else 100.0
r=np.fromfile(sys.argv[1],'<f4').reshape(-1,ch); o=np.fromfile(sys.argv[2],'<f4').reshape(-1,ch)
if len(r)!=len(o):
    print("length differs: ref %d ours %d"%(len(r),len(o))); sys.exit(2)
a=slice(min(20000,len(r)//4),len(r)-min(20000,len(r)//4)); ok=True; out=[]
for c in range(ch):
    x=r[a,c].astype(np.float64); y=o[a,c].astype(np.float64)
    e=((x-y)**2).sum(); s=(x**2).sum()
    snr=200.0 if e==0 else 10*np.log10(max(s,1e-30)/e)
    out.append("%.1f"%snr); ok=ok and snr>=lo
print("channels",ch,"SNR dB:"," ".join(out)); sys.exit(0 if ok else 1)
