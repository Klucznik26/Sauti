#!/usr/bin/env python3
"""Writes Ogg Vorbis streams with hand-made setups (floor 0 or 1, residue 0 / 1 / 2, coupling, any channel count and block sizes) and random audio packets: build(...) + write_ogg(...); the main part compares our decoder with libvorbisfile on a grid of them (FLOOR=0|1, TOL=, NORES=, EXTREME=).
Test tool (not a product) of the Vorbis audit, see docs/VORBIS.md.  Needs libvorbisfile.so.3 (the reference decoder, through ctypes); run from the repository root with `python3 -I`."""
import struct,random,math,sys,os,subprocess,numpy as np
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)));import vorbis_ref as vf
class BW:
    def __init__(s):s.b=[];s.n=0
    def put(s,v,w):
        for i in range(w):s.b.append((v>>i)&1)
    def bytes(s):
        b=s.b+[0]*((-len(s.b))%8);out=bytearray()
        for i in range(0,len(b),8):out.append(sum(b[i+j]<<j for j in range(8)))
        return bytes(out)
def f32pack(x):
    if x==0:return 0
    sign=0
    if x<0:sign=1;x=-x
    e=math.floor(math.log2(x))+1;m=int(round(x/(2.0**(e-21))))
    if m>=1<<21:m>>=1;e+=1
    return (sign<<31)|((e-21+788)<<21)|m
def ilog(x):
    n=0
    while x>0:n+=1;x>>=1
    return n
def book_vec(bw,dim,entries,lenbits,values,vb,mn=-1.0,dl=0.5):
    bw.put(0x564342,24);bw.put(dim,16);bw.put(entries,24);bw.put(0,1);bw.put(0,1)
    for _ in range(entries):bw.put(lenbits-1,5)
    bw.put(1,4);bw.put(f32pack(mn),32);bw.put(f32pack(dl),32);bw.put(vb-1,4);bw.put(0,1)
    for i in range(values):bw.put(i%(1<<vb),vb)
def book_vec2(bw,dim,entries,lenbits,vb,mn,dl,fl0=False):
    bw.put(0x564342,24);bw.put(dim,16);bw.put(entries,24);bw.put(0,1);bw.put(0,1)
    for _ in range(entries):bw.put(lenbits-1,5)
    bw.put(2,4);bw.put(f32pack(mn),32);bw.put(f32pack(dl),32);bw.put(vb-1,4);bw.put(0,1)
    for e in range(entries):
        for j in range(dim):bw.put((j+(e%2)) if fl0 else (e%3)+j+1,vb)
def book_scalar(bw,entries,lenbits):
    bw.put(0x564342,24);bw.put(1,16);bw.put(entries,24);bw.put(0,1);bw.put(0,1)
    for _ in range(entries):bw.put(lenbits-1,5)
    bw.put(0,4)
def build(path,ch,rate,bs0,bs1,rtype,nmodes=2,npk=60,seed=1,coupling=True,floor_t=0):
    rng=random.Random(seed)
    ident=b'\x01vorbis'+struct.pack('<IBIiii',0,ch,rate,0,128000,0)+bytes([ilog(bs0)-1|((ilog(bs1)-1)<<4)])[0:0]
    ident=b'\x01vorbis'+struct.pack('<I',0)+bytes([ch])+struct.pack('<Iiii',rate,0,128000,0)+bytes([(int(math.log2(bs0)))|(int(math.log2(bs1))<<4),1])
    comm=b'\x03vorbis'+struct.pack('<I',4)+b'test'+struct.pack('<I',0)+b'\x01'
    bw=BW();bw.put(5,8);bw.b+=[0]*0
    for c in b'vorbis':bw.put(c,8)
    # books: 0 floor0 (dim4,16e), 1 classbook (scalar 4e dim 1... must be dim>=1: classwords per codeword = dim), 2 residue vq (dim 4, 16e)
    nbooks=4;bw.put(nbooks-1,8)
    (book_vec2(bw,4,16,4,3,0.0,0.12) if os.environ.get('EXTREME') else book_vec2(bw,4,16,4,4,math.pi/64*4.25,math.pi/64*4,fl0=True))  # book 0: floor0 vectors, strictly rising inside a vector (the LSP angles must be ordered)
    book_scalar(bw,4,2)              # book 1: classbook dim 1 entries 4 (classifications 4)
    book_vec(bw,4,16,4,2,2)          # book 2: residue vq
    book_scalar(bw,16,4)             # book 3: floor1 Y values
    bw.put(0,6);bw.put(0,16)         # time domain
    bw.put(0,6)                      # 1 floor
    if floor_t==0:
        bw.put(0,16)
        order=8;bw.put(order,8);bw.put(rate//2 if rate//2<65536 else 65535,16);bw.put(64,16);(bw.put(8,6),bw.put(50,8)) if os.environ.get('EXTREME') else (bw.put(2,6),bw.put(1,8));bw.put(0,4);bw.put(0,8)
    else:
        bw.put(1,16);bw.put(1,5);bw.put(0,4)           # 1 partition of class 0
        bw.put(0,3);bw.put(0,2);bw.put(4,8)           # class 0: dim 1, no subclasses, subclass book 3 (+1)
        bw.put(0,2);bw.put(8,4);bw.put(77,8)           # multiplier 1 (range 256), rangebits 8, X = 77
    bw.put(0,6)                      # 1 residue
    bw.put(rtype,16);part=24;end=bs1//2 if rtype!=2 else (bs1//2)*ch
    bw.put(0,24);bw.put((0 if os.environ.get('NORES') else end) if rtype!=2 else (bs1//2)*ch,24);bw.put(part-1,24);bw.put(4-1,6);bw.put(1,8)  # 4 classes, classbook 1
    for c in range(4):bw.put(1,3);bw.put(0,1)   # cascade: pass0 only
    for c in range(4):bw.put(2,8)                # book per class pass 0
    bw.put(0,6)                      # 1 mapping
    bw.put(0,16);bw.put(0,1)         # type0, 1 submap
    if ch>1 and coupling:
        bw.put(1,1);bw.put(1-1,8);bw.put(0,ilog(ch-1));bw.put(1,ilog(ch-1))
    else:bw.put(0,1)
    bw.put(0,2)
    bw.put(0,8);bw.put(0,8);bw.put(0,8)
    bw.put(nmodes-1,6)
    for m in range(nmodes):bw.put(m&1,1);bw.put(0,16);bw.put(0,16);bw.put(0,8)
    bw.put(1,1)
    setup=bw.bytes()
    pk=[]
    prev=None
    modes=[rng.randrange(nmodes) for _ in range(npk)]
    for i in range(npk):
        b=BW();b.put(0,1);mode=modes[i];b.put(mode,ilog(nmodes-1))
        if mode&1:
            pl=(modes[i-1]&1) if i>0 else 1
            nl=(modes[i+1]&1) if i+1<npk else 1
            b.put(pl,1);b.put(nl,1)
        raw=bytes(rng.randrange(256) for _ in range(rng.randrange(60,400)))
        data=b.bytes()
        # splice: header bits then random bits: append random after bit position
        bits=b.b[:]
        for byte in raw:
            for j in range(8):bits.append((byte>>j)&1)
        bb=BW();bb.b=bits;pk.append(bb.bytes())
    return ident,comm,setup,pk
_t=None
def crc(d):
    global _t
    if _t is None:
        _t=[]
        for i in range(256):
            c=i<<24
            for _ in range(8):c=((c<<1)^0x04c11db7)&0xffffffff if c&0x80000000 else (c<<1)&0xffffffff
            _t.append(c)
    c=0
    for b in d:c=((c<<8)&0xffffffff)^_t[((c>>24)&255)^b]
    return c
def page(serial,seq,flags,granule,packets_data,cont=False):
    segs=[]
    for p in packets_data:
        n=len(p)
        while n>=255:segs.append(255);n-=255
        segs.append(n)
    hdr=b'OggS'+bytes([0,flags])+struct.pack('<qIII',granule,serial,seq,0)+bytes([len(segs)])+bytes(segs)
    body=b''.join(packets_data);d=hdr+body;c=crc(d)
    return d[:22]+struct.pack('<I',c)+d[26:]
def write_ogg(path,ident,comm,setup,pk,granule_end):
    out=b''
    out+=page(1,0,2,0,[ident]);out+=page(1,1,0,0,[comm,setup])
    seq=2
    # audio packets one per page except grouped by up to 3 (granule -1 for pages not ending... use real granules: unknown => set -1 except last)
    i=0
    while i<len(pk):
        grp=pk[i:i+4];i+=len(grp);last=i>=len(pk)
        out+=page(1,seq,4 if last else 0,granule_end if last else -1,grp);seq+=1
    open(path,'wb').write(out)
if __name__=='__main__':
    CLI='build/maktabasauti_cli';T=(os.environ.get('TMPDIR') or '/tmp')+'/vorbis_gen/';os.makedirs(T,exist_ok=True)
    bad=0;n=0
    for rtype in (0,1,2):
        for ch,coupling in ((1,False),(2,True),(2,False),(3,False),(6,False)):
            for (bs0,bs1) in ((256,2048),(512,512),(128,1024)):
                for seed in (1,2):
                    ident,comm,setup,pk=build(None,ch,44100,bs0,bs1,rtype,seed=seed,coupling=coupling,floor_t=int(os.environ.get('FLOOR','1')))
                    p=T+f'g_{rtype}_{ch}_{int(coupling)}_{bs0}_{bs1}_{seed}.ogg'
                    write_ogg(p,ident,comm,setup,pk,granule_end=100000)
                    try:x,_,tot=vf.decode(p)
                    except Exception as e:print(p,'lib open fail');continue
                    o=subprocess.run([CLI,p,T+'o.f32'],capture_output=True,text=True)
                    n+=1
                    if o.returncode or 'open:' in o.stdout+o.stderr:print(os.path.basename(p),'OURS FAIL',(o.stdout+o.stderr).strip()[-50:],'lib frames',len(x));bad+=1;continue
                    a=np.fromfile(T+'o.f32','<f4').reshape(-1,ch)
                    m=min(len(a),len(x));d=np.abs(a[:m]-x[:m]).max() if m else 0
                    ok=len(a)==len(x) and d<float(os.environ.get('TOL','1e-4'))*max(1,np.abs(x).max())
                    if not ok:bad+=1
                    print(os.path.basename(p),len(a),len(x),'diff %.2e peak %.2e'%(d,np.abs(x).max()),'OK' if ok else 'BAD')
    print('bad',bad,'of',n)
