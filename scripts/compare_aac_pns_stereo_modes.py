#!/usr/bin/env python3
"""Compare diagnostic PNS modes against the same FFmpeg PCM fixture.

Non-destructive experiment: production decoder stays unchanged; outputs written
to build/audio-fixtures/aac-29/diagnostic-stereo. Never modifies strict gate.
"""
import argparse
from array import array
import json
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parent.parent

def run(cmd):
    return subprocess.run([str(x) for x in cmd],cwd=ROOT,stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT,text=True,check=False)

def read_pcm(path):
    samples=array("f")
    samples.frombytes(path.read_bytes())
    if sys.byteorder!="little":
        samples.byteswap()
    return samples

def inspect_transient_blocks(native,reference,frame_numbers=(0,1,12,21,22)):
    """Show where the difference actually lies inside transient frames.

    Summarize each 128-sample time slice in each channel without
    amplitude fitting, alignment shifts, or changes to the strict gate.
    """
    count=min(len(native),len(reference))//2048
    for frame in frame_numbers:
        if frame>=count:
            continue
        for channel,label in ((0,"L"),(1,"R")):
            scored=[]
            for block in range(8):
                a=frame*2048+block*256+channel
                b=a+256
                native_part=native[a:b:2]
                ref_part=reference[a:b:2]
                energy=sum(float(y)*y for y in ref_part)
                diff=sum((float(x)-float(y))**2
                         for x,y in zip(native_part,ref_part))
                peak=max(abs(float(x)-float(y))
                         for x,y in zip(native_part,ref_part))
                scored.append((block,(diff/max(energy,1e-22))**0.5,
                               diff,peak,energy))
            worst=max(scored,key=lambda x:x[2])
            print("frame %d channel %s worst_128=%d nRMSE=%.5f "
                  "peak_err=%.7f ref_energy=%.8g"%
                  (frame,label,worst[0],worst[1],worst[3],worst[4]))

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--case",default="transient_44100_128k")
    args=ap.parse_args()
    base=ROOT/"build/audio-fixtures/aac-29"/args.case
    audio=base.with_suffix(".aac")
    reference=base.with_suffix(".reference.f32")
    if not audio.is_file() or not reference.is_file():
        ap.error("missing fixture: run make inspect-aac-29-full first")
    build=run(["make","build/test_aac_real_stereo",
               "build/test_aac_real_stereo_pns_independent",
               "build/test_aac_real_stereo_pns_positive",
               "build/test_aac_real_stereo_pns_zero",
               "build/test_aac_real_stereo_windows_sine"])
    if build.returncode:
        print(build.stdout[-4000:],file=sys.stderr)
        return 2
    out=ROOT/"build/audio-fixtures/aac-29/diagnostic-stereo"
    out.mkdir(parents=True,exist_ok=True)
    scores={}
    for name,exe in (("current","build/test_aac_real_stereo"),
                     ("independent","build/test_aac_real_stereo_pns_independent"),
                     ("positive","build/test_aac_real_stereo_pns_positive"),
                     ("zero","build/test_aac_real_stereo_pns_zero"),
                     ("sine_windows","build/test_aac_real_stereo_windows_sine")):
        pcm=out/(args.case+"."+name+".f32")
        mask=out/(args.case+"."+name+".mask")
        decode=run([ROOT/exe,audio,pcm,mask])
        if decode.returncode:
            print(decode.stdout[-2000:],file=sys.stderr)
            return 2
        verify=run([sys.executable,ROOT/"tools/verify_aac_stereo_strict.py",
                    pcm,reference,mask,"--json"])
        try:
            scores[name]=json.loads(verify.stdout)
        except ValueError:
            print(verify.stdout[-2000:],file=sys.stderr)
            return 2
        channels=scores[name].get("channels",{})
        print("%s %s: LEFT nRMSE=%s RIGHT nRMSE=%s"%
              (args.case,name,channels.get("LEFT",{}).get("nrmse"),
               channels.get("RIGHT",{}).get("nrmse")))
    baseline=(out/(args.case+".current.f32")).read_bytes()
    for variant in ("independent","positive","zero","sine_windows"):
        other=(out/(args.case+"."+variant+".f32")).read_bytes()
        print("PCM bit-identical current vs %s: %s"%(variant,baseline==other))
    inspect_transient_blocks(
        read_pcm(out/(args.case+".current.f32")),read_pcm(reference))
    print("Strict gate (unchanged): "+", ".join(
        "%s=%s"%(key,value.get("ok")) for key,value in scores.items()))
    (out/(args.case+".json")).write_text(json.dumps(scores,indent=2))
    print("Diagnostic report: %s"%(out/(args.case+".json")))
    return 0

if __name__=="__main__":
    sys.exit(main())
