#!/usr/bin/env python3
"""libvorbisfile as an oracle: decode(path) -> (float32 frames, rate, ov_pcm_total).
Test tool (not a product) of the Vorbis audit, see docs/VORBIS.md.  Needs libvorbisfile.so.3 (the reference decoder, through ctypes); run from the repository root with `python3 -I`."""
import ctypes,sys,numpy as np
vf=ctypes.CDLL('libvorbisfile.so.3')
class Info(ctypes.Structure):_fields_=[('version',ctypes.c_int),('channels',ctypes.c_int),('rate',ctypes.c_long),('bu',ctypes.c_long),('bn',ctypes.c_long),('bl',ctypes.c_long),('bw',ctypes.c_long),('codec',ctypes.c_void_p)]
vf.ov_info.restype=ctypes.POINTER(Info);vf.ov_pcm_total.restype=ctypes.c_int64
def decode(path):
    buf=ctypes.create_string_buffer(2048)
    if vf.ov_fopen(path.encode(),buf)!=0: raise RuntimeError('open')
    info=vf.ov_info(buf,-1).contents;ch=info.channels;rate=info.rate
    total=vf.ov_pcm_total(buf,-1)
    out=[];bs=ctypes.c_int()
    pp=ctypes.POINTER(ctypes.POINTER(ctypes.c_float))()
    while True:
        n=vf.ov_read_float(buf,ctypes.byref(pp),4096,ctypes.byref(bs))
        if n<=0:break
        blk=np.stack([np.ctypeslib.as_array(pp[c],shape=(n,)).copy() for c in range(ch)],1);out.append(blk)
    vf.ov_clear(buf)
    return (np.concatenate(out) if out else np.zeros((0,ch),np.float32)),rate,total
if __name__=='__main__':
    for p in sys.argv[1:]:
        x,r,t=decode(p);print(p,x.shape,r,'total',t)
