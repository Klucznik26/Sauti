#!/usr/bin/env python3
"""Regenerates tests/data/dvsm_adpcm.dvs (a Falcon DVSM file with Pack 5 = ADPCM) and tests/data/dvsm_adpcm.pcm (the expected 16-bit big-endian stereo PCM), and the same for Pack 4 (Voice): dvsm_voice.dvs / dvsm_voice.pcm.

Both come from the ORIGINAL routines of WinRec Pro 2.11H (WIN_REC.PRG from the WinCut Pro 1.3 package of the Atari TOSser archive; copyrighted, kept OUTSIDE the repository, never executed as a program):
the routine at 0x8586 encodes a synthetic stereo signal (a rising tone plus noise on the left, a tone on the right, a full-scale square burst that saturates the predictor, a stretch of silence) and the routine
at 0x866e decodes it; both are run in a Unicorn M68K emulation (pip install unicorn), nothing but their OUTPUT is stored.  The test (tests/test_dvsm_winrec.c) requires our decoder to reproduce the PCM bit for bit.
usage: tests/data/make_dvsm_adpcm_fixture.py WIN_REC.PRG   (run from tests/data; writes next to the script)"""
import struct, math, random, sys, os
PRG = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser('~/Sauti-tools/winrec-pro2/x/WINRECUT.PRO/WINREC.PRO/WIN_REC.PRG')
from unicorn import *
from unicorn.m68k_const import *
def load(fn,base=0):
    d=open(fn,'rb').read()
    magic,tl,dl,bl,sl,_,_,absflag=struct.unpack('>HIIIIIIH',d[:28])
    img=bytearray(d[28:28+tl+dl])
    rel=d[28+tl+dl+sl:]
    # relocation table
    off=struct.unpack('>I',rel[:4])[0];p=4
    if off:
        a=off
        while True:
            struct.pack_into('>I',img,a,(struct.unpack_from('>I',img,a)[0]+base)&0xffffffff)
            while True:
                b=rel[p];p+=1
                if b==0:a=None;break
                if b==1:a+=254;continue
                a+=b;break
            if a is None:break
    return img,tl,dl,bl

img, tl, dl, bl = load(PRG)
uc = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN)
uc.ctl_set_cpu_model(UC_CPU_M68K_M68040)
uc.mem_map(0, 0x600000); uc.mem_write(0, bytes(img))
STACK = 0x500000; SENT = 0x4f0000; ST = 0x297de; IN = 0x300000; OUT = 0x400000   # ST: the state of the left channel in the program's BSS (4 bytes: predictor word, index byte)
uc.mem_write(SENT, b'\x4e\x71')
def run(entry, count, a0, a1, chan, state):
    uc.mem_write(ST, state)
    sp = STACK - 4; uc.mem_write(sp, struct.pack('>I', ST)); sp -= 4; uc.mem_write(sp, struct.pack('>I', SENT))
    uc.reg_write(UC_M68K_REG_A7, sp); uc.reg_write(UC_M68K_REG_D0, count); uc.reg_write(UC_M68K_REG_D1, chan)
    uc.reg_write(UC_M68K_REG_A0, a0); uc.reg_write(UC_M68K_REG_A1, a1)
    uc.emu_start(entry, SENT, count=50_000_000)
N=9000
random.seed(20261124)
sig=[]
for i in range(N):
    t=i/49170.0
    l=9000*math.sin(2*math.pi*(300+2500*t)*t)+random.gauss(0,300)
    r=7000*math.sin(2*math.pi*440*t+1)+random.gauss(0,200)
    if 1500<=i<1560: l=32767 if (i//4)%2 else -32768; r=-32768 if (i//4)%2 else 32767   # full-scale square burst: predictor saturation
    if 2600<=i<2800: l=r=0                                                         # silence: index falls to 0
    sig.append((int(max(-32768,min(32767,l))),int(max(-32768,min(32767,r)))))
pcm=b''.join(struct.pack('>hh',l,r) for l,r in sig)
uc.mem_write(IN,pcm);uc.mem_write(OUT,b'\0'*(N+8))
for chan in (0,1):
    run(0x8586,N,IN+(2 if chan else 0),OUT,chan,b'\0\0\0\0')
adpcm=bytes(uc.mem_read(OUT,N))
# decode with the original decoder routine, both channels, one pass
uc.mem_write(IN,adpcm);uc.mem_write(OUT,b'\0'*(N*4+8))
for chan in (0,1):
    run(0x866e,N,IN,OUT+(2 if chan else 0),chan,b'\0\0\0\0')
dec=bytes(uc.mem_read(OUT,N*4))
import numpy as np
x=np.frombuffer(pcm,'>i2').astype(float);y=np.frombuffer(dec,'>i2').astype(float)
print('encode->decode SNR %.1f dB'%(10*np.log10((x**2).sum()/((x-y)**2).sum())),'clips',(abs(y)>=32767).sum())
hdr=b'DVSM\0\0'+struct.pack('>HHBBI',16,7,5,1,0)
open(os.path.join(os.path.dirname(os.path.abspath(__file__)),'dvsm_adpcm.dvs'),'wb').write(hdr+adpcm)
open(os.path.join(os.path.dirname(os.path.abspath(__file__)),'dvsm_adpcm.pcm'),'wb').write(dec)
print(len(hdr+adpcm),len(dec))

# ---- Pack 4 (Voice), stereo: blocks of 4 header bytes (left and right 16-bit seeds) and one byte per frame (left code high nibble, right low), expected PCM from the routine at 0x7252 (one call per block)
TAB = 0x297e6
VT = [0, 64, 256, 512, 1024, 2048, 4096, 8192] + [0] * 8                     # the table the program's init routine builds (voice codes 0..7; code c >= 8 is -table[16 - c])
uc.mem_write(TAB, struct.pack('>16h', *VT))
def vdelta(c): return VT[c] if c < 8 else -VT[16 - c]
def venc(xs, seed):                                                          # greedy encoder: the code whose reconstruction is closest to the target, never leaving the 16-bit range
    out = []; cur = seed
    for x in xs:
        best = min((c for c in range(16) if -32768 <= cur + vdelta(c) <= 32767), key=lambda c: abs(cur + vdelta(c) - x))
        cur += vdelta(best); out.append(best)
    return out
BL = 1004                                                                     # block: 4 + 1000 frames; the last block is partial (4 + 300)
parts = [BL, BL, BL, 4 + 300]
dvs = b''; pcm = b''; pos = 0
for k, nbytes in enumerate(parts):
    nframes = nbytes - 4
    xs = [(int(6000 * math.sin(2 * math.pi * (180 + 90 * k) * (pos + i) / 49170.0) + random.gauss(0, 150)), int(5000 * math.sin(2 * math.pi * 330 * (pos + i) / 49170.0 + k) + random.gauss(0, 100))) for i in range(nframes)]
    sl, sr = xs[0][0], xs[0][1]
    cl = venc([x[0] for x in xs], sl); cr = venc([x[1] for x in xs], sr)
    blk = struct.pack('>hh', sl, sr) + bytes((a << 4) | b for a, b in zip(cl, cr))
    if k == 0:                                                              # all 16 codes on both channels (the greedy encoder never emits code 8), small enough not to overflow
        raw = bytearray(blk); raw[4 + 50:4 + 66] = bytes((i << 4) | (15 - i) for i in range(16)); blk = bytes(raw)
    uc.mem_write(IN, blk); uc.mem_write(OUT, b'\0' * (4 * (nbytes + 4)))
    uc.mem_write(0x4aa68, b'\0\0\0\0'); uc.mem_write(0x4aae8, struct.pack('>I', 1 << 24))   # counters of the first channel set: fresh block, long block
    sp = STACK - 4; uc.mem_write(sp, struct.pack('>I', SENT))
    uc.reg_write(UC_M68K_REG_A7, sp); uc.reg_write(UC_M68K_REG_D0, 0); uc.reg_write(UC_M68K_REG_D1, nbytes)
    uc.reg_write(UC_M68K_REG_A0, IN); uc.reg_write(UC_M68K_REG_A1, OUT)
    uc.emu_start(0x7252, SENT, count=50_000_000)
    dec = bytes(uc.mem_read(OUT, 4 * (nframes + 1)))
    pcm += dec; dvs += blk; pos += nframes
hdr = b'DVSM\0\0' + struct.pack('>HHBBI', 16, 7, 4, 1, BL)
here = os.path.dirname(os.path.abspath(__file__))
open(os.path.join(here, 'dvsm_voice.dvs'), 'wb').write(hdr + dvs)
open(os.path.join(here, 'dvsm_voice.pcm'), 'wb').write(pcm)
print('voice', len(hdr + dvs), len(pcm))
