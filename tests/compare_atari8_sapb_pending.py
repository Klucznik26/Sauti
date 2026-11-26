#!/usr/bin/env python3
"""Compare unresolved ASMA entries to an external ASAP binary, no source or music imports."""
import argparse
import csv
from pathlib import Path
import subprocess
import tempfile
import wave

def inspect(path):
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
        return f"{channels}ch {frames/rate:.3f}s {onset}"

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("archive",type=Path)
    p.add_argument("report",type=Path)
    p.add_argument("--asap",required=True,type=Path)
    p.add_argument("--renderer",type=Path,default=Path("build/render_atari8_sapb"))
    p.add_argument("--seconds",type=int,default=15,choices=range(1,61))
    a=p.parse_args()
    if not a.asap.is_file() or not a.renderer.is_file():
        p.error("ASAP or Sauti executable missing")
    root=a.archive.resolve()
    with a.report.open(newline="",encoding="utf-8") as f:
        rows=[r for r in csv.DictReader(f) if r["result"]!="rendered nonzero"]
    with tempfile.TemporaryDirectory(prefix="sauti-reference-") as tmp:
        for r in rows:
            src=(root/r["file"]).resolve()
            if not src.is_relative_to(root) or not src.is_file():
                print("Invalid path:",r["file"],flush=True)
                continue
            header=src.read_bytes().split(b"\xff\xff",1)[0]
            print("\n"+r["file"],flush=True)
            if r["result"]=="INIT/parse 2":
                print(header.decode("ascii",errors="replace").strip(),flush=True)
            for label,exe in (("Sauti",a.renderer),("ASAP",a.asap)):
                output=Path(tmp)/"out.wav"
                output.unlink(missing_ok=True)
                if label=="Sauti":
                    cmd=[str(exe.resolve()),str(src),str(output),str(a.seconds)]
                else:
                    cmd=[str(exe.resolve()),"-o",str(output),"-t",
                         f"{a.seconds//60:02d}:{a.seconds%60:02d}","-R","48000",str(src)]
                try:
                    run=subprocess.run(cmd,capture_output=True,text=True,errors="replace",timeout=60)
                    if run.returncode:
                        msg=(run.stderr+run.stdout).strip()
                        print(label+": "+msg,flush=True)
                    else:
                        print(label+": "+inspect(output),flush=True)
                except (OSError,EOFError,wave.Error,subprocess.TimeoutExpired) as e:
                    print(label+": "+str(e),flush=True)

if __name__=="__main__":
    main()
