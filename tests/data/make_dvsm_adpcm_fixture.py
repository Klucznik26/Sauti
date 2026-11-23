#!/usr/bin/env python3
"""Regenerates tests/data/dvsm_adpcm.dvs (a Falcon DVSM file with Pack 5 = ADPCM) and tests/data/dvsm_adpcm.pcm (the expected 16-bit big-endian stereo PCM).

Both come from the ORIGINAL routines of WinRec Pro 2.11H (WIN_REC.PRG from the WinCut Pro 1.3 package of the Atari TOSser archive; copyrighted, kept OUTSIDE the repository, never executed as a program):
the routine at 0x8586 encodes a synthetic stereo signal (a rising tone plus noise on the left, a tone on the right, a full-scale square burst that saturates the predictor, a stretch of silence) and the routine
at 0x866e decodes it; both are run in a Unicorn M68K emulation (pip install unicorn), nothing but their OUTPUT is stored.  The test (tests/test_dvsm_adpcm.c) requires our decoder to reproduce the PCM bit for bit.
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
