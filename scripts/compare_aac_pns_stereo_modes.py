#!/usr/bin/env python3
"""Compare diagnostic PNS modes against the same FFmpeg PCM fixture.

Non-destructive experiment: production decoder stays unchanged; outputs written
to build/audio-fixtures/aac-29/diagnostic-stereo. Never modifies strict gate.
"""
import argparse
import json
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parent.parent

def run(cmd):
    return subprocess.run([str(x) for x in cmd],cwd=ROOT,stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT,text=True,check=False)

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
               "build/test_aac_real_stereo_pns_zero"])
    if build.returncode:
        print(build.stdout[-4000:],file=sys.stderr)
        return 2
    out=ROOT/"build/audio-fixtures/aac-29/diagnostic-stereo"
    out.mkdir(parents=True,exist_ok=True)
    scores={}
    for name,exe in (("current","build/test_aac_real_stereo"),
                     ("independent","build/test_aac_real_stereo_pns_independent"),
                     ("positive","build/test_aac_real_stereo_pns_positive"),
                     ("zero","build/test_aac_real_stereo_pns_zero")):
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
    for variant in ("independent","positive","zero"):
        other=(out/(args.case+"."+variant+".f32")).read_bytes()
        print("PCM bit-identical current vs %s: %s"%(variant,baseline==other))
    print("Strict gate (unchanged): "+", ".join(
        "%s=%s"%(key,value.get("ok")) for key,value in scores.items()))
    (out/(args.case+".json")).write_text(json.dumps(scores,indent=2))
    print("Diagnostic report: %s"%(out/(args.case+".json")))
    return 0

if __name__=="__main__":
    sys.exit(main())
