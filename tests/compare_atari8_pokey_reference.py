#!/usr/bin/env python3
"""Generate original minimal SAP-B fixtures, compare Sauti and external ASAP.
No imported player code or third-party audio; output is diagnostic, not a pass/fail oracle."""
import argparse
import math
import struct
import subprocess
import wave
from pathlib import Path

def fixture(ctl, regs, changes=()):
    # INIT: SKCTL=3, configure registers, STIMER, RTS.
    init = bytearray()
    def store(code, reg, value):
        code.extend((0xa9, value, 0x8d, reg, 0xd2))
    store(init, 15, 3)
    store(init, 8, ctl)
    for reg, value in regs:
        store(init, reg, value)
    store(init, 9, 0)
    init.append(0x60)
    # PLAYER optionally alternates two register values every call.
    player = bytearray()
    if changes:
        player.extend((0xad, 0x00, 0x22, 0x49, 1, 0x8d, 0x00, 0x22, 0xf0, 0))
        for reg, a, b in changes:
            store(player, reg, a)
        player.append(0x60)
        player[9] = len(player)-10
        for reg, a, b in changes:
            store(player, reg, b)
    player.append(0x60)
    header = b'SAP\r\nTYPE B\r\nINIT 2000\r\nPLAYER 2100\r\nTIME 00:04\r\n'
    def segment(addr, data):
        return struct.pack("<HH", addr, addr+len(data)-1)+data
    return header+b'\xff\xff'+segment(0x2000,init)+segment(0x2100,player)+segment(0x2200,b'\x00')

def measure(path):
    with wave.open(str(path),"rb") as w:
        if (w.getnchannels(),w.getsampwidth(),w.getframerate(),w.getnframes()) != (1,2,48000,192000):
            raise ValueError("unexpected WAV shape")
        raw=w.readframes(192000)
        if len(raw)!=384000:
            raise ValueError("truncated WAV")
    x=struct.unpack("<192000h",raw)
    # Ignore the initial and final seconds; estimate rising zero crossings
    # and AC RMS. Crossing frequency is meaningful only for pure tones.
    x=x[48000:144000]
    mean=sum(x)/len(x)
    rms=math.sqrt(sum((v-mean)**2 for v in x)/len(x))
    crossings=sum(a<=mean<b for a,b in zip(x,x[1:]))
    return rms,crossings/2

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--asap",required=True,type=Path)
    p.add_argument("--renderer",type=Path,default=Path("build/render_atari8_sapb"))
    p.add_argument("--out",type=Path,default=Path("build/pokey-reference"))
    a=p.parse_args()
    a.out.mkdir(parents=True,exist_ok=True)
    cases=[]
    # Expected steady-state pure tone frequencies from divider formula.
    clock=1773447
    for ctl,div in ((0,28),(1,114),(0x40,1)):
        for n in (7,31,127):
            cases.append((f"tone_{ctl:02x}_{n}",ctl,[(0,n),(1,0xaf)],(),clock/(2*((n+4) if div==1 else div*(n+1)))))
    for ctl in (0x10,0x50,0x70):
        for n in (0x140,0x3a0,0x1234):
            cases.append((f"link_{ctl:02x}_{n:04x}",ctl,[(0,n&255),(2,n>>8),(3,0xaf)],(),clock/(2*((n+7) if ctl&0x40 else 28*(n+1)))))
    for mode in (0x8f,0xcf,0x6f,0x2f):
        cases.append((f"noise_{mode:02x}",0x50,[(0,0x40),(2,1),(3,mode)],(),None))
    cases.append(("volume_dac",0x70,[(1,0x10)],[(1,0x10,0x1f)],None))
    cases.append(("audctl_switch",0x50,[(0,0x40),(2,1),(3,0xcf)],[(8,0x50,0x70)],None))
    print("case | expected Hz | Sauti crossings/s RMS | ASAP crossings/s RMS",flush=True)
    for name,ctl,regs,changes,expected in cases:
        sap=a.out/(name+".sap")
        sap.write_bytes(fixture(ctl,regs,changes))
        values=[]
        for label,exe in (("Sauti",a.renderer),("ASAP",a.asap)):
            wav=a.out/(name+"-"+label+".wav")
            wav.unlink(missing_ok=True)
            cmd=([str(exe.resolve()),str(sap),str(wav),"4","0"] if label=="Sauti"
                 else [str(exe.resolve()),"-o",str(wav),"-t","00:04","-R","48000","-s","0",str(sap)])
            try:
                subprocess.run(cmd,check=True,capture_output=True,text=True,timeout=60)
                rms,hz=measure(wav)
                values.append(f"{hz:8.1f} {rms:8.1f}")
            except (OSError,ValueError,wave.Error,subprocess.SubprocessError) as e:
                values.append(f"ERROR {e}")
        print(f"{name:24} | {expected if expected is not None else '-':>10} | "+" | ".join(values),flush=True)

if __name__=="__main__":
    main()
