#!/usr/bin/env python3
"""Generate original minimal SAP-B fixtures, compare Sauti and external ASAP.
No imported player code or third-party audio; output is diagnostic, not a pass/fail oracle."""
import argparse
import math
import cmath
import struct
import subprocess
import wave
from pathlib import Path

def fixture(ctl, regs, changes=(), delay=0):
    # INIT: SKCTL=3, configure registers, STIMER, RTS.
    init = bytearray()
    def store(code, reg, value):
        code.extend((0xa9, value, 0x8d, reg, 0xd2))
    store(init, 15, 3)
    store(init, 8, ctl)
    for reg, value in regs:
        store(init, reg, value)
    init.extend([0xea]*delay)  # 2 CPU cycles per NOP before STIMER
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

def peak_hz(x, mean):
    # Hann window, zero-padded radix-2 FFT (standard library only).
    n=1 << (len(x)-1).bit_length()
    a=[complex((v-mean)*(0.5-0.5*math.cos(2*math.pi*i/(len(x)-1)))) for i,v in enumerate(x)]
    a.extend([0j]*(n-len(a)))
    j=0
    for i in range(1,n):
        bit=n>>1
        while j & bit:
            j ^= bit
            bit >>= 1
        j ^= bit
        if i<j:
            a[i],a[j]=a[j],a[i]
    width=2
    while width<=n:
        half=width//2
        roots=[cmath.exp(-2j*math.pi*k/width) for k in range(half)]
        for start in range(0,n,width):
            for k,w in enumerate(roots):
                i=start+k
                t=w*a[i+half]
                a[i+half]=a[i]-t
                a[i]+=t
        width*=2
    k=max(range(1,n//2),key=lambda i:abs(a[i]))
    return k*48000/n if abs(a[k])>1e-6 else 0.0


def poly4_rates(period):
    # Hardware pattern documented in Altirra Hardware Reference Manual.
    # Independent of Sauti and ASAP source; phase is intentionally unknown.
    pattern="000111011001010"
    count=15//math.gcd(period,15)
    rates=set()
    for phase in range(15):
        bits=[pattern[(phase+i*period)%15] for i in range(count)]
        rises=sum(bits[i-1]=="0" and bits[i]=="1" for i in range(count))
        rates.add(round(1773447*rises/(period*count),1))
    return "/".join(f"{v:.1f}" for v in sorted(rates))


def measure(path, spectrum=False):
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
    return rms,crossings/2,peak_hz(x,mean) if spectrum else None

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--asap",required=True,type=Path)
    p.add_argument("--renderer",type=Path,default=Path("build/render_atari8_sapb"))
    p.add_argument("--out",type=Path,default=Path("build/pokey-reference"))
    mode=p.add_mutually_exclusive_group()
    mode.add_argument("--poly4-phase",action="store_true",help="run isolated phase sweep with spectral peaks")
    mode.add_argument("--mixer",action="store_true",help="measure relative DAC amplitude at different constant channel biases")
    mix=p.add_mutually_exclusive_group()
    mix.add_argument("--nonlinear-mix",action="store_true",help="explicitly select the default saturation curve")
    mix.add_argument("--linear-mix",action="store_true",help="select legacy linear mixer")
    a=p.parse_args()
    a.out.mkdir(parents=True,exist_ok=True)
    cases=[]
    # Expected steady-state pure tone frequencies from divider formula.
    clock=1773447
    for ctl,div in ((0,28),(1,114),(0x40,1)):
        for n in ((63,127,255) if div==1 else (7,31,127)):
            cases.append((f"tone_{ctl:02x}_{n}",ctl,[(0,n),(1,0xaf)],(),clock/(2*((n+4) if div==1 else div*(n+1)))))
    for ctl in (0x10,0x50,0x70):
        for n in (0x140,0x3a0,0x1234):
            cases.append((f"link_{ctl:02x}_{n:04x}",ctl,[(0,n&255),(2,n>>8),(3,0xaf)],(),clock/(2*((n+7) if ctl&0x40 else 28*(n+1)))))
    for mode in (0x8f,0xcf,0x6f,0x2f):
        cases.append((f"noise_{mode:02x}",0x50,[(0,0x40),(2,1),(3,mode)],(),None))
    cases.append(("volume_dac",0x70,[(1,0x10)],[(1,0x10,0x1f)],None))
    cases.append(("audctl_switch",0x50,[(0,0x40),(2,1),(3,0xcf)],[(8,0x50,0x70)],None))
    cases=[(*case,0) for case in cases]
    if a.poly4_phase:
        cases=[]
        # Include slow tone for which mean crossings were misleading.
        cases.append(("slow_6hz",0x10,[(0,0x34),(2,0x12),(3,0xaf)],(),6.8,0))
        for period,phases in ((327,15),(325,5),(328,1),(330,1)):
            n=period-7
            for delay in range(phases):
                cases.append((f"poly4_{period}_nop{delay:02}",0x50,
                              [(0,n&255),(2,n>>8),(3,0xcf)],(),poly4_rates(period),delay))
    if a.mixer:
        cases=[]
        # One channel alternates 0/volume at the PAL player cadence.
        # Other channels are constant DAC outputs; these shift total
        # digital level without introducing additional AC signals.
        for bias in (0,15,30,45):
            for volume in (1,2,4,8,15):
                regs=[(1,0x10)]
                for ch in range(1,4):
                    regs.append((2*ch+1,0x10|(15 if ch<=bias//15 else 0)))
                cases.append((f"dac_bias{bias:02}_vol{volume:02}",0,regs,
                              [(1,0x10,0x10|volume)],None,0))
        # Independent zero-bias pure-tone amplitude sweep.
        for volume in (1,2,4,8,15):
            cases.append((f"tone_vol{volume:02}",0,[(0,31),(1,0xa0|volume)],
                          (),clock/(2*28*32),0))
    print("case | expected Hz or allowed raw rising edges/s | Sauti crossings/s RMS | ASAP crossings/s RMS"
          +(" (plus FFT peak Hz per renderer)" if a.poly4_phase else ""),flush=True)
    mixer_rows={}
    for name,ctl,regs,changes,expected,delay in cases:
        sap=a.out/(name+".sap")
        sap.write_bytes(fixture(ctl,regs,changes,delay))
        values=[]
        amplitudes={}
        for label,exe in (("Sauti",a.renderer),("ASAP",a.asap)):
            wav=a.out/(name+"-"+label+".wav")
            wav.unlink(missing_ok=True)
            cmd=([str(exe.resolve()),str(sap),str(wav),"4","0"] if label=="Sauti"
                 else [str(exe.resolve()),"-o",str(wav),"-t","00:04","-R","48000","-s","0",str(sap)])
            if label=="Sauti" and a.nonlinear_mix:
                cmd.append("--nonlinear-mix")
            elif label=="Sauti" and a.linear_mix:
                cmd.append("--linear-mix")
            try:
                subprocess.run(cmd,check=True,capture_output=True,text=True,timeout=60)
                rms,hz,peak=measure(wav,a.poly4_phase)
                amplitudes[label]=rms
                values.append(f"{hz:8.1f} {rms:8.1f}"+(f" peak={peak:8.1f}" if peak is not None else ""))
            except (OSError,ValueError,wave.Error,subprocess.SubprocessError) as e:
                values.append(f"ERROR {e}")
        target=f"{expected:.1f}" if isinstance(expected,(int,float)) else (expected or "-")
        print(f"{name:24} | {target:>10} | "+" | ".join(values),flush=True)
        if a.mixer:
            mixer_rows[name]=amplitudes
    if a.mixer:
        print("\nRelative DAC amplitude: same volume, bias 0 = 100% (per renderer)")
        print("bias volume | Sauti % | ASAP %")
        for bias in (0,15,30,45):
            for vol in (1,2,4,8,15):
                row=mixer_rows[f"dac_bias{bias:02}_vol{vol:02}"]
                base=mixer_rows[f"dac_bias00_vol{vol:02}"]
                values=[f"{100*row[k]/base[k]:8.2f}" if k in row and base.get(k,0)>0 else "N/A" for k in ("Sauti","ASAP")]
                print(f"{bias:4} {vol:6} | "+" | ".join(values))
        print("\nVolume response: zero bias, volume 15 = 100% (per renderer and signal type)")
        for prefix in ("dac_bias00_vol","tone_vol"):
            base=mixer_rows[prefix+"15"]
            for vol in (1,2,4,8,15):
                row=mixer_rows[prefix+f"{vol:02}"]
                values=[f"{100*row[k]/base[k]:8.2f}" if k in row and base.get(k,0)>0 else "N/A" for k in ("Sauti","ASAP")]
                print(f"{prefix}{vol:02} | "+" | ".join(values))

if __name__=="__main__":
    main()
