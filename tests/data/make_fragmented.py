import struct,sys
def parse(buf,off,end):
    out=[]
    while off+8<=end:
        sz,typ=struct.unpack('>I4s',buf[off:off+8]); hdr=8
        if sz==1: sz=struct.unpack('>Q',buf[off+8:off+16])[0]; hdr=16
        if sz==0: sz=end-off
        out.append((typ,off,hdr,sz)); off+=sz
    return out
CONT=(b'moov',b'trak',b'mdia',b'minf',b'stbl')
def rebuild(buf,off,hdr,sz,typ):
    payload=buf[off+hdr:off+sz]
    if typ in CONT:
        inner=b''.join(rebuild(payload,o-0,h,s,t) for t,o,h,s in parse(payload,0,len(payload)))
        return struct.pack('>I4s',8+len(inner),typ)+inner
    if typ in (b'stts',b'stsc',b'stco',b'co64'): payload=payload[:4]+struct.pack('>I',0)          # entry_count = 0
    if typ==b'stsz': payload=payload[:4]+struct.pack('>II',0,0)                                  # sample_size 0, count 0
    return struct.pack('>I4s',8+len(payload),typ)+payload
src=open(sys.argv[1],'rb').read()
top=parse(src,0,len(src))
ftyp=[b for b in top if b[0]==b'ftyp'][0]; moov=[b for b in top if b[0]==b'moov'][0]
# samples from the original tables
def find(buf,off,end,path):
    for t,o,h,s in parse(buf,off,end):
        if t==path[0]:
            return (o,h,s) if len(path)==1 else find(buf,o+h,o+s,path[1:])
stbl=find(src,moov[1]+moov[2],moov[1]+moov[3],[b'trak',b'mdia',b'minf',b'stbl'])
def leaf(t):
    o,h,s=find(src,stbl[0]+stbl[1],stbl[0]+stbl[2],[t]); return src[o+h:o+s]
stsz=leaf(b'stsz'); stco=leaf(b'stco'); stsc=leaf(b'stsc')
count=struct.unpack('>I',stsz[8:12])[0]; sizes=list(struct.unpack('>%dI'%count,stsz[12:12+4*count]))
nch=struct.unpack('>I',stco[4:8])[0]; chunks=list(struct.unpack('>%dI'%nch,stco[8:8+4*nch]))
nsc=struct.unpack('>I',stsc[4:8])[0]; groups=[struct.unpack('>III',stsc[8+12*i:20+12*i]) for i in range(nsc)]
samples=[];s=0
for gi,(first,per,_) in enumerate(groups):
    last=groups[gi+1][0] if gi+1<nsc else nch+1
    for c in range(first,last):
        off=chunks[c-1]
        for k in range(per):
            if s<count: samples.append((off,sizes[s])); off+=sizes[s]; s+=1
newmoov=rebuild(src,moov[1],moov[2],moov[3],b'moov')
trex=struct.pack('>I4sIIIIII',32,b'trex',0,1,1,0,0,0)
mvex=struct.pack('>I4s',8+len(trex),b'mvex')+trex
newmoov=struct.pack('>I4s',8+len(newmoov)-8+len(mvex),b'moov')+newmoov[8:]+mvex
out=bytearray(src[ftyp[1]:ftyp[1]+ftyp[3]])+newmoov
seq=1
for i in range(0,len(samples),24):
    grp=samples[i:i+24]
    data=b''.join(src[o:o+z] for o,z in grp)
    mfhd=struct.pack('>I4sII',16,b'mfhd',0,seq); seq+=1
    tfhd=struct.pack('>I4sII',16,b'tfhd',0x20000,1)
    trun_len=8+4+4+4+8*0+4*len(grp)                        # header, flags, count, data_offset, sizes
    trun=struct.pack('>I4sIII',trun_len,b'trun',0x201,len(grp),0)+b''.join(struct.pack('>I',z) for o,z in grp)
    traf=struct.pack('>I4s',8+len(tfhd)+len(trun),b'traf')+tfhd+trun
    moof_len=8+len(mfhd)+len(traf)
    # data_offset: from moof start to first sample = moof_len + 8 (mdat header)
    trun=trun[:16]+struct.pack('>I',moof_len+8)+trun[20:]
    traf=struct.pack('>I4s',8+len(tfhd)+len(trun),b'traf')+tfhd+trun
    moof=struct.pack('>I4s',8+len(mfhd)+len(traf),b'moof')+mfhd+traf
    out+=moof+struct.pack('>I4s',8+len(data),b'mdat')+data
open(sys.argv[2],'wb').write(bytes(out))
