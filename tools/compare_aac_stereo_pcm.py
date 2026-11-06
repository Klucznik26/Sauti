#!/usr/bin/env python3
"""AAC stereo LR interleaved f32 diagnostic, comparing channels separately."""
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from compare_aac_pcm import load_pcm,load_mask,evaluate,FRAME

def main():
    if len(sys.argv)!=4:
        print("usage: compare_aac_stereo_pcm.py native.f32 reference.f32 frame.mask")
        return 2
    native,ref=load_pcm(sys.argv[1]),load_pcm(sys.argv[2])
    rows=load_mask(sys.argv[3])
    if len(native)!=len(rows)*FRAME*2 or len(ref)%2:
        print("FAIL stereo PCM lengths do not match interleaved sample frames")
        return 1
    for ch,name in enumerate(("LEFT","RIGHT")):
        print("======= AAC stereo %s =======" % name)
        n=native[ch::2]
        r=ref[ch::2]
        status=evaluate(n,r,rows,None)
        if status:return status
    return 0

if __name__=="__main__":
    sys.exit(main())
