#!/usr/bin/env python3
"""SBR-82: PCM of the channel decoder against another decoder's PCM (f32le, 2 channels from the reference, left channel used).

usage: sauti_he_pcm_vs_ffmpeg.py REF.f32 OURS.f32 [MIN_HF_DB=100] [MIN_LOW_DB=100]
Same time base (no delay compensation: the channel decoder is aligned with the reference to the sample).  Hann windows of 2048, frames 20.. step 2,
bands split at 5.5 kHz at 48 kHz.  Exit 0 when both band SNRs reach the minimum, 1 otherwise, 2 when there is too little data."""
import sys
import numpy as np

def main():
    ref=np.fromfile(sys.argv[1],'<f4').reshape(-1,2)[:,0]
    ours=np.fromfile(sys.argv[2],'<f4')
    hf_min=float(sys.argv[3]) if len(sys.argv)>3 else 100.0
    low_min=float(sys.argv[4]) if len(sys.argv)>4 else 100.0
    n=min(len(ref),len(ours))
    if n<2048*24:
        print("too little data (%d samples)"%n); return 2
    w=np.hanning(2048); freq=np.fft.rfftfreq(2048,1/48000); mask=freq>=5500
    num=den=lnum=lden=0.0
    for f in range(20,n//2048-1,2):
        A=np.fft.rfft(ours[f*2048:(f+1)*2048]*w); B=np.fft.rfft(ref[f*2048:(f+1)*2048]*w)
        num+=np.sum(np.abs(B[mask])**2); den+=np.sum(np.abs(A[mask]-B[mask])**2)
        lnum+=np.sum(np.abs(B[~mask])**2); lden+=np.sum(np.abs(A[~mask]-B[~mask])**2)
    hf=10*np.log10(num/max(den,1e-300)); low=10*np.log10(lnum/max(lden,1e-300))
    print("LOW %.1f dB  HF %.2f dB  (%d samples)"%(low,hf,n))
    ok=hf>=hf_min and low>=low_min
    print("WYNIK:","ZGODNE" if ok else "NIEZGODNE")
    return 0 if ok else 1

sys.exit(main())
