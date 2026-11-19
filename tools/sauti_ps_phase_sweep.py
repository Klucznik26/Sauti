#!/usr/bin/env python3
"""SBR-96: compares PS stereo output of two decoders on constant-parameter streams (tones in the lowest hybrid bands).

usage: sauti_ps_phase_sweep.py A.f32 B.f32 [C.f32]   (interleaved float32 stereo, 48 kHz)
Delay and gain differ between decoders, so the features compared are the complex channel ratio L/R at six tone frequencies and the level of each tone
relative to the 700 Hz tone.  Prints the largest relative difference A-B (and C-B).  Exit 0 always; the shell test applies the threshold."""
import sys
import numpy as np
FREQS=[40,90,180,300,700,1500]
def cplx(path):
    x=np.fromfile(path,'<f4').reshape(-1,2); n=48000*3; seg=x[48000:48000+n]
    F=np.fft.rfft(seg*np.hanning(n)[:,None],axis=0)
    return np.array([[F[int(round(f*n/48000)),c] for c in (0,1)] for f in FREQS])
def feat(x): return np.concatenate([x[:,0]/x[:,1],(np.abs(x[:,0])/np.abs(x[4,0]))+0j])
def rel(x,y): return float((np.abs(x-y)/np.maximum(np.abs(y),0.05)).max())
a,b=feat(cplx(sys.argv[1])),feat(cplx(sys.argv[2]))
out="A-B %.4f"%rel(a,b)
if len(sys.argv)>3: out+=" C-B %.4f"%rel(feat(cplx(sys.argv[3])),b)
print(out)
