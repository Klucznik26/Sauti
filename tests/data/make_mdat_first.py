import struct,sys
# fragmentowany MP4, w ktorym kazde mdat stoi PRZED swoim moof (data_offset ujemny): poprawny plik, ale ze strumienia nieprzewijalnego nie do odczytania
def parse(buf,off,end):
    out=[]
    while off+8<=end:
        sz,typ=struct.unpack('>I4s',buf[off:off+8]); out.append((typ,off,sz)); off+=sz
    return out
src=open(sys.argv[1],'rb').read(); top=parse(src,0,len(src)); out=bytearray(); i=0
while i<len(top):
    t,o,s=top[i]
    if t==b'moof' and i+1<len(top) and top[i+1][0]==b'mdat':
        moof=bytearray(src[o:o+s]); mdat=src[top[i+1][1]:top[i+1][1]+top[i+1][2]]
        k=bytes(moof).index(b'trun'); struct.pack_into('>i',moof,k+12,8-len(mdat))
        out+=mdat+moof; i+=2
    else: out+=src[o:o+s]; i+=1
open(sys.argv[2],'wb').write(out)
