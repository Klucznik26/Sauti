#!/usr/bin/env python3
"""Differential fuzzing: bytes of the audio packets of the given files are mutated (the CRC of every page is recomputed), both decoders must agree wherever libvorbisfile reports no error.
Test tool (not a product) of the Vorbis audit, see docs/VORBIS.md.  Needs libvorbisfile.so.3 (the reference decoder, through ctypes); run from the repository root with `python3 -I`."""
import sys,os,struct,subprocess,numpy as np,random,zlib
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)));import vorbis_ref as vf,ctypes
CLI='build/maktabasauti_cli';T=(os.environ.get('TMPDIR') or '/tmp')+'/vorbis_fuzz/';os.makedirs(T,exist_ok=True)
def crc(data):
    c=0
    for b in data:
        c^=b<<24
        for _ in range(8): c=((c<<1)^0x04c11db7)&0xffffffff if c&0x80000000 else (c<<1)&0xffffffff
    return c
_t=None
def crc_fast(data):
    global _t
    if _t is None:
        _t=[]
        for i in range(256):
            c=i<<24
            for _ in range(8): c=((c<<1)^0x04c11db7)&0xffffffff if c&0x80000000 else (c<<1)&0xffffffff
            _t.append(c)
    c=0
    for b in data: c=((c<<8)&0xffffffff)^_t[((c>>24)&255)^b]
    return c
def pages(d):
    p=0;out=[]
    while p+27<=len(d):
        n=d[p+26];h=27+n;sz=sum(d[p+27:p+27+n]);out.append((p,h,sz));p+=h+sz
    return out
def mutate(d,rng,k):
    d=bytearray(d);pg=pages(d)
    if len(pg)<4: return None
    # skip first 3 pages (headers); mutate payload of later pages
    cand=[x for x in pg[3:] if x[2]>20]
    for _ in range(k):
        p,h,sz=rng.choice(cand);i=p+h+rng.randrange(sz);d[i]=rng.randrange(256) if rng.random()<.5 else d[i]^(1<<rng.randrange(8))
    for p,h,sz in pg[3:]:
        d[p+22:p+26]=b'\0\0\0\0';c=crc_fast(bytes(d[p:p+h+sz]));d[p+22:p+26]=struct.pack('<I',c)
    return bytes(d)
vf.vf.ov_read_float.restype=ctypes.c_long
def libdecode(path):
    buf=ctypes.create_string_buffer(2048)
    if vf.vf.ov_fopen(path.encode(),buf)!=0:return None
    info=vf.vf.ov_info(buf,-1).contents;ch=info.channels;out=[];bs=ctypes.c_int();holes=0
    pp=ctypes.POINTER(ctypes.POINTER(ctypes.c_float))()
    while True:
        n=vf.vf.ov_read_float(buf,ctypes.byref(pp),4096,ctypes.byref(bs))
        if n==0:break
        if n<0: holes+=1;
        if n<0:
            if holes>50:break
            continue
        out.append(np.stack([np.ctypeslib.as_array(pp[c],shape=(n,)).copy() for c in range(ch)],1))
    vf.vf.ov_clear(buf)
    return (np.concatenate(out) if out else np.zeros((0,ch),np.float32)),holes
seeds=sys.argv[1:] or ['tests/data/vorbis/ch6.ogg','tests/data/vorbis/pages.ogg','tests/data/vorbis/cont.ogg']
rng=random.Random(7);stats={'same':0,'diff':0,'lib_err':0,'ours_fail':0,'ours_fail_lib_ok':0}
for s in seeds:
    d=open(s,'rb').read()
    for it in range(40):
        m=mutate(d,rng,rng.choice([1,2,5,20]))
        if m is None:break
        open(T+'m.ogg','wb').write(m)
        L=libdecode(T+'m.ogg')
        o=subprocess.run([CLI,T+'m.ogg',T+'o.f32'],capture_output=True,text=True)
        if L is None or L[1]>0: stats['lib_err']+=1;continue
        x=L[0]
        if o.returncode or 'read:' in o.stdout+o.stderr: stats['ours_fail_lib_ok']+=1;print('OURS FAILS but lib ok',s,it,(o.stdout+o.stderr).strip()[-60:]);open(T+f'keep_{stats["ours_fail_lib_ok"]}.ogg','wb').write(m);continue
        a=np.fromfile(T+'o.f32','<f4').reshape(-1,x.shape[1])
        if len(a)!=len(x) or np.abs(a-x).max()>1e-5: stats['diff']+=1;print('DIFF',s,it,len(a),len(x),np.abs(a[:len(x)]-x[:len(a)]).max());open(T+f'diff_{stats["diff"]}.ogg','wb').write(m)
        else: stats['same']+=1
print(stats)
