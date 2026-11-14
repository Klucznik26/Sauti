#!/usr/bin/env python3
"""AAC-29: controlled FFmpeg encoder-tool ablation on one stereo WAV.

Compare native AAC decoding against FFmpeg for the SAME encoded bitstream.
Do not change quality thresholds or infer bit-exact PNS from PCM correlation.
"""
import argparse
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from verify_aac_stereo_strict import assess
from compare_aac_pcm import load_pcm, load_mask


def run(cmd):
    p=subprocess.run([str(s) for s in cmd],cwd=ROOT,text=True,
                     stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    if p.returncode:
        raise RuntimeError("command failed (%d): %s\n%s" %
                           (p.returncode," ".join(map(str,cmd)),p.stdout))
    return p.stdout


def case(wav,rate,kbps,output,pns,tns):
    key="pns_%s_tns_%s"%("on" if pns else "off",
                         "on" if tns else "off")
    folder=output/key
    folder.mkdir(parents=True,exist_ok=True)
    aac=folder/"test.aac"
    native=folder/"native.f32"
    ref=folder/"reference.f32"
    mask=folder/"frames.mask"
    run(["ffmpeg","-hide_banner","-nostdin","-loglevel","error","-y",
         "-i",wav,"-ac","2","-ar",rate,"-c:a","aac",
         "-b:a","%sk"%kbps,"-aac_pns",int(pns),"-aac_tns",int(tns),
         "-f","adts",aac])
    run(["ffmpeg","-hide_banner","-nostdin","-loglevel","error","-y",
         "-i",aac,"-ac","2","-ar",rate,"-f","f32le",ref])
    trace=run([ROOT/"build/test_aac_real_stereo",aac,native,mask])
    (folder/"native.log").write_text(trace,encoding="utf-8")
    metrics=assess(load_pcm(native),load_pcm(ref),load_mask(mask))
    summary={"pns":pns,"tns":tns,"ok":metrics["ok"],
             "metrics":metrics}
    (folder/"result.json").write_text(json.dumps(summary,indent=2),
                                      encoding="utf-8")
    return summary


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rate",type=int,choices=(32000,44100,48000),
                        default=44100)
    parser.add_argument("--bitrate",type=int,choices=(64,128,192,256),
                        default=128)
    parser.add_argument("--kind",choices=("tonal","transient","dense"),
                        default="transient")
    args=parser.parse_args()
    wav=ROOT/"build/audio-fixtures/aac-29/source"/str(args.rate)/(args.kind+".wav")
    if not wav.exists():
        raise SystemExit("missing WAV fixture: "+str(wav))
    output=ROOT/"build/audio-fixtures/aac-29/ablation"/(
        "%s_%d_%dk"%(args.kind,args.rate,args.bitrate))
    results=[]
    for pns,tns in ((1,1),(0,1),(1,0),(0,0)):
        result=case(wav,args.rate,args.bitrate,output,pns,tns)
        results.append(result)
        metrics=result["metrics"]
        print("PNS=%s TNS=%s: %s frames=%s/%s"%
              (pns,tns,"PASS" if result["ok"] else "FAIL",
               metrics["decoded"],metrics["frames"]))
        for label,score in metrics.get("channels",{}).items():
            print("  %s: corr=%.7f nRMSE=%.6f RMS=%.6f"%(
                label,score["corr"],score["nrmse"],score["ratio"]))
    summary=output/"summary.json"
    summary.write_text(json.dumps(results,indent=2),encoding="utf-8")
    print("AAC-29 controlled diagnostic:",summary)
    # A failed comparison is an important experiment result, not a script
    # failure. Keep all individual verdicts explicit and machine readable.
    return 0


if __name__=="__main__":
    try:
        sys.exit(main())
    except (OSError,RuntimeError,ValueError) as exc:
        print("AAC-29 diagnostic failed:",exc,file=sys.stderr)
        sys.exit(2)
