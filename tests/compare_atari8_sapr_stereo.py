#!/usr/bin/env python3
"""Compare local SAP-R raw stereo PCM with external ASAP original SAP audio.
The dump is video-frame sampled, so this is an envelope comparison,
not a bit-exact oracle. No third-party music or code is distributed.
"""
import argparse
import math
import struct
import subprocess
import tempfile
import wave
from pathlib import Path

RATE = 48000

def pearson(x, y):
    mx, my = sum(x)/len(x), sum(y)/len(y)
    xx = sum((a-mx)**2 for a in x)
    yy = sum((b-my)**2 for b in y)
    if not xx or not yy:
        return None
    return sum((a-mx)*(b-my) for a,b in zip(x,y))/math.sqrt(xx*yy)

def stats(raw, frames):
    if len(raw) != frames*4:
        raise ValueError("unexpected stereo PCM byte count")
    samples = struct.unpack(f"<{frames*2}h",raw)
    result = []
    for ch in range(2):
        x = samples[ch::2]
        power = sum(v*v for v in x)
        # Ten 100ms bins per second, no channel summation.
        envelope = [math.sqrt(sum(v*v for v in x[i:i+4800])/4800)
                    for i in range(0,frames,4800)]
        result.append((math.sqrt(power/frames),envelope,
                       sum(v in (-32768,32767) for v in x)))
    return result

def report(label, measured, reference):
    print(label)
    for ch, (m,r) in enumerate(zip(measured,reference)):
        rms,env,clips=m
        rr,renv,rclips=r
        db=f"{20*math.log10(rms/rr):+.3f}" if rms and rr else "N/A"
        corr=pearson(env,renv)
        cs=f"{corr:.6f}" if corr is not None else "N/A"
        print(f"  {'LR'[ch]}: RMS {rms:.2f} / {rr:.2f}; level {db} dB; "
              f"100ms envelope r={cs}; endpoint samples {clips}/{rclips}")
    swapped=[pearson(measured[i][1],reference[1-i][1]) for i in range(2)]
    print("  swapped L/R envelope r: "+", ".join(
        f"{v:.6f}" if v is not None else "N/A" for v in swapped))

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("sap",type=Path)
    p.add_argument("linear",type=Path)
    p.add_argument("nonlinear",type=Path)
    p.add_argument("--asap",required=True,type=Path)
    p.add_argument("--song",type=int,default=0)
    p.add_argument("--seconds",type=int,default=30)
    p.add_argument("--expected-frames",type=int,required=True)
    a=p.parse_args()
    if not 1<=a.seconds<=120 or not 0<=a.song<=255:
        p.error("seconds must be 1..120 and song 0..255")
    frames=a.seconds*RATE
    if not frames<=a.expected_frames<=RATE*600:
        p.error("invalid expected SAP-R frame count")
    try:
        data=[]
        for path in (a.linear,a.nonlinear):
            size=path.stat().st_size
            if size!=a.expected_frames*4:
                raise ValueError(f"{path}: expected {a.expected_frames*4} bytes, got {size}")
            with path.open("rb") as f:
                data.append(stats(f.read(frames*4),frames))
            print(f"{path}: exact {a.expected_frames} stereo PCM frames")
        with tempfile.TemporaryDirectory(prefix="sapr-reference-") as tmp:
            wav=Path(tmp)/"ref.wav"
            t=f"{a.seconds//60:02}:{a.seconds%60:02}"
            subprocess.run([str(a.asap.resolve()),"-o",str(wav),"-t",t,
                            "-R",str(RATE),"-s",str(a.song),str(a.sap.resolve())],
                           check=True,timeout=5*a.seconds+60)
            with wave.open(str(wav),"rb") as w:
                if (w.getnchannels(),w.getsampwidth(),w.getframerate(),w.getnframes())!=(2,2,RATE,frames):
                    raise ValueError("unexpected ASAP WAV shape")
                ref=stats(w.readframes(frames),frames)
        print(f"Comparison: first {a.seconds}s, 100ms RMS bins")
        report("Linear SAP-R vs original SAP",data[0],ref)
        report("Nonlinear SAP-R vs original SAP",data[1],ref)
    except (OSError, ValueError, wave.Error, subprocess.SubprocessError) as e:
        p.error(str(e))

if __name__=="__main__":
    main()
