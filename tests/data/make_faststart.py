import struct,sys
# MP4 z moov na koncu -> moov przed mdat (faststart): przesuwa moov i dodaje jego rozmiar do offsetow w stco/co64.
def parse(buf,off,end):
    out=[]
    while off+8<=end:
        sz,typ=struct.unpack('>I4s',buf[off:off+8]); hdr=8
        if sz==1: sz=struct.unpack('>Q',buf[off+8:off+16])[0]; hdr=16
        if sz==0: sz=end-off
        out.append((typ,off,hdr,sz)); off+=sz
    return out
CONT=(b'moov',b'trak',b'mdia',b'minf',b'stbl')
def shift(buf,off,hdr,sz,typ,add):
    payload=bytearray(buf[off+hdr:off+sz])
    if typ in CONT:
        inner=b''.join(shift(payload,o,h,s,t,add) for t,o,h,s in parse(payload,0,len(payload)))
        return struct.pack('>I4s',8+len(inner),typ)+inner
    if typ==b'stco':
        n=struct.unpack('>I',payload[4:8])[0]
        for i in range(n): struct.pack_into('>I',payload,8+4*i,struct.unpack('>I',payload[8+4*i:12+4*i])[0]+add)
    if typ==b'co64':
        n=struct.unpack('>I',payload[4:8])[0]
        for i in range(n): struct.pack_into('>Q',payload,8+8*i,struct.unpack('>Q',payload[8+8*i:16+8*i])[0]+add)
    return struct.pack('>I4s',8+len(payload),typ)+bytes(payload)
src=open(sys.argv[1],'rb').read()
top=parse(src,0,len(src))
moov=[b for b in top if b[0]==b'moov'][0]
newmoov=shift(src,moov[1],moov[2],moov[3],b'moov',len(src[moov[1]:moov[1]+moov[3]]))
assert len(newmoov)==moov[3]
out=b''.join(src[o:o+s] for t,o,h,s in top if t not in (b'moov',b'mdat'))+newmoov+b''.join(src[o:o+s] for t,o,h,s in top if t==b'mdat')
open(sys.argv[2],'wb').write(out)
