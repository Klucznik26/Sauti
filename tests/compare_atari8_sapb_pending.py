#!/usr/bin/env python3
"""Compare unresolved ASMA entries to an external ASAP binary, no source or music imports."""
import argparse
import csv
from pathlib import Path
import subprocess
import re
import tempfile
import wave
import hashlib
import math
import struct

def inspect(path, stats=False):
    with wave.open(str(path), "rb") as w:
        if w.getsampwidth() != 2:
            return f"unexpected width {w.getsampwidth()}"
        frames, rate, channels = w.getnframes(), w.getframerate(), w.getnchannels()
        data = w.readframes(frames)
        if len(data) != frames * channels * 2:
            return "truncated WAV"
        first = next((i for i in range(0,len(data),channels*2)
                      if any(data[i:i+channels*2])),None)
        onset = "silence" if first is None else f"onset {first/(channels*2*rate):.3f}s"
        result=f"{channels}ch {frames/rate:.3f}s {onset}"
        if stats:
            # Per-second signed-PCM energy, including silence. Not a
            # waveform match or psychoacoustic quality measurement.
            for start in range(0,frames,rate):
                count=min(rate,frames-start)
                block=data[start*channels*2:(start+count)*channels*2]
                samples=struct.unpack("<"+"h"*(count*channels),block)
                peak=max(map(abs,samples),default=0)
                rms=math.sqrt(sum(x*x for x in samples)/len(samples))
                active=sum(x!=0 for x in samples)
                result+=f"\n  {start/rate:5.1f}s peak={peak:5d} rms={rms:8.2f} nonzero={active}/{len(samples)}"
        return result

def inspect_image(src, message):
    raw=src.read_bytes()
    off=raw.find(b"\xff\xff")
    if off<0:
        return
    ram=bytearray(65536)
    present=bytearray(65536)
    pos=off
    blocks=[]
    while pos<len(raw):
        if raw[pos:pos+2]==b"\xff\xff":
            pos+=2
        if pos+4>len(raw):
            return
        lo=int.from_bytes(raw[pos:pos+2],"little")
        hi=int.from_bytes(raw[pos+2:pos+4],"little")
        pos+=4
        size=hi-lo+1
        if size<=0 or pos+size>len(raw):
            return
        ram[lo:hi+1]=raw[pos:pos+size]
        present[lo:hi+1]=b"\x01"*size
        blocks.append(f"{lo:04X}-{hi:04X}")
        pos+=size
    print("Image blocks:",", ".join(blocks))
    header=raw[:off].decode("ascii",errors="replace")
    addresses={}
    for tag,addr in re.findall(r"(?m)^(INIT|PLAYER) ([0-9A-Fa-f]+)\r?$",header):
        addresses[tag]=int(addr,16)
    match=re.search(r"first transition to \$0000 from PC=([0-9A-Fa-f]+)",message)
    if match:
        addresses["JUMP"]=int(match[1],16)
    for label,addr in addresses.items():
        if not 0<=addr<=65535:
            continue
        start=max(0,addr-8)
        end=min(65536,addr+16)
        print(f"{label} {addr:04X} original {start:04X}:",
              " ".join(f"{ram[i]:02X}" if present[i] else "--" for i in range(start,end)))
    if "JUMP" in addresses:
        for target in (addresses["JUMP"]+1,addresses["JUMP"]+2):
            if target>65535:
                continue
            candidates=[f"{i:04X}:{ram[i]:02X}" for i in range(65534)
                        if ram[i] in (0x8c,0x8d,0x8e,0x99,0x9d)
                        and all(present[i:i+3])
                        and ram[i+1]==(target&255) and ram[i+2]==target>>8]
            print(f"Candidate stores to {target:04X}:",", ".join(candidates) or "none")

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("archive",type=Path)
    p.add_argument("report",type=Path)
    p.add_argument("--asap",required=True,type=Path)
    p.add_argument("--renderer",type=Path,default=Path("build/render_atari8_sapb"))
    p.add_argument("--seconds",type=int,default=15,choices=range(1,61))
    p.add_argument("--only",help="case-insensitive substring of relative SAP path")
    p.add_argument("--stats",action="store_true",help="print per-second PCM peak and RMS")
    p.add_argument("--frame-restart",action="store_true",help="opt in to experimental Sauti scheduler")
    a=p.parse_args()
    if not a.asap.is_file() or not a.renderer.is_file():
        p.error("ASAP or Sauti executable missing")
    root=a.archive.resolve()
    with a.report.open(newline="",encoding="utf-8") as f:
        rows=[r for r in csv.DictReader(f) if r["result"]!="rendered nonzero"
              and (not a.only or a.only.casefold() in r["file"].casefold())]
    if not rows:
        p.error("no matching pending files")
    with tempfile.TemporaryDirectory(prefix="sauti-reference-") as tmp:
        for r in rows:
            src=(root/r["file"]).resolve()
            if not src.is_relative_to(root) or not src.is_file():
                print("Invalid path:",r["file"],flush=True)
                continue
            header=src.read_bytes().split(b"\xff\xff",1)[0]
            print("\n"+r["file"],flush=True)
            if a.stats:
                print("SHA256:",hashlib.sha256(src.read_bytes()).hexdigest(),flush=True)
            if r["result"]=="INIT/parse 2":
                print(header.decode("ascii",errors="replace").strip(),flush=True)
            for label,exe in (("Sauti",a.renderer),("ASAP",a.asap)):
                output=Path(tmp)/"out.wav"
                output.unlink(missing_ok=True)
                if label=="Sauti":
                    cmd=[str(exe.resolve()),str(src),str(output),str(a.seconds)]
                    if a.frame_restart:
                        cmd.append("--frame-restart")
                else:
                    cmd=[str(exe.resolve()),"-o",str(output),"-t",
                         f"{a.seconds//60:02d}:{a.seconds%60:02d}","-R","48000",str(src)]
                try:
                    run=subprocess.run(cmd,capture_output=True,text=True,errors="replace",timeout=60)
                    if run.returncode:
                        msg=(run.stderr+run.stdout).strip()
                        print(label+": "+msg,flush=True)
                        if label=="Sauti" and "machine status 4" in msg:
                            inspect_image(src,msg)
                    else:
                        print(label+": "+inspect(output,a.stats),flush=True)
                        if label=="Sauti" and run.stdout.strip():
                            print(run.stdout.strip(),flush=True)
                except (OSError,EOFError,wave.Error,subprocess.TimeoutExpired) as e:
                    print(label+": "+str(e),flush=True)

if __name__=="__main__":
    main()
