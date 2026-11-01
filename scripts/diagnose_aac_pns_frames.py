#!/usr/bin/env python3
"""AAC-LC diagnostic: correlate native-vs-FFmpeg PCM errors with per-frame tools.

Reads pre-existing AAC-29 matrix fixtures. Does not change conformance gates,
the native decoder, audio samples or encoder options. The separate decoder
trace is metadata only: PCM comes from the already saved matrix results.
"""
import argparse
from array import array
from collections import Counter
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
FRAME = 1024
FRAME_LINE = re.compile(r"^  frame (\d+): (decoded|unsupported|invalid)")
TOOLS_LINE = re.compile(
    r"^    tools: groups=(\d+) max_sfb=(\d+) TNS=(\d+)/(\d+)"
    r" PNS=(\d+)/(\d+) MS=(\d+) intensity=(\d+)"
)
RNG_LINE = re.compile(r"^    pns_rng: before=([0-9a-f]{8}) after=([0-9a-f]{8})")
WINDOW_LINE = re.compile(
    r"^    windows L=sequence:(\d+) shape:(\d+) R=sequence:(\d+) shape:(\d+)"
)

def load(path):
    data = array("f")
    with path.open("rb") as handle:
        data.frombytes(handle.read())
    if sys.byteorder != "little":
        data.byteswap()
    return data

def trace_tools(aac, decoder):
    proc = subprocess.run([str(decoder), str(aac)], cwd=ROOT,
                          env={**__import__("os").environ,
                               "SAUTI_AAC_TRACE_WINDOWS": "1"},
                          text=True, capture_output=True, check=False)
    if proc.returncode:
        raise RuntimeError("native metadata trace failed: " + proc.stdout[-1500:] +
                           proc.stderr[-800:])
    records = {}
    current = None
    for line in proc.stdout.splitlines():
        match = FRAME_LINE.match(line)
        if match:
            current = int(match.group(1))
            records[current] = {"status": match.group(2)}
            continue
        match = RNG_LINE.match(line)
        if match and current is not None:
            records[current]["rng_before"] = match.group(1)
            records[current]["rng_after"] = match.group(2)
            continue
        match = TOOLS_LINE.match(line)
        if match and current is not None:
            values = [int(v) for v in match.groups()]
            records[current].update(dict(groups=values[0], max_sfb=values[1],
                tns=values[2]+values[3], pns_left=values[4],
                pns_right=values[5], ms=values[6], intensity=values[7]))
            continue
        match = WINDOW_LINE.match(line)
        if match and current is not None:
            values = [int(v) for v in match.groups()]
            records[current].update(window_left=values[0],
                                    window_right=values[2])
    return records

def frame_scores(native, reference, n):
    start = n*FRAME*2
    out = []
    for channel in range(2):
        a = native[start+channel:start+FRAME*2:2]
        b = reference[start+channel:start+FRAME*2:2]
        square_ref = sum(float(y)*y for y in b)
        square_delta = sum((float(x)-float(y))**2 for x,y in zip(a,b))
        dot = sum(float(x)*y for x,y in zip(a,b))
        square_native = sum(float(x)*x for x in a)
        correlation = dot / (square_ref*square_native)**.5 if square_ref*square_native>1e-22 else None
        out.append({"nrmse":(square_delta/max(square_ref,1e-22))**.5,
                    "max_error":max(abs(float(x)-float(y)) for x,y in zip(a,b)),
                    "correlation":correlation, "reference_energy":square_ref})
    return out

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--case", default="transient_44100_128k")
    parser.add_argument("--fixture-dir", default="build/audio-fixtures/aac-29")
    parser.add_argument("--top", type=int, default=20)
    args=parser.parse_args()
    root=Path(args.fixture_dir)
    if not root.is_absolute():
        root=ROOT/root
    base=root/args.case
    aac=base.with_suffix(".aac")
    native_file=base.with_suffix(".native.f32")
    reference_file=base.with_suffix(".reference.f32")
    decoder=ROOT/"build/test_aac_real_stereo"
    for path in (aac,native_file,reference_file,decoder):
        if not path.is_file():
            parser.error("missing %s (run make inspect-aac-29-full first)"%path)
    native,ref=load(native_file),load(reference_file)
    if len(native)!=len(ref) or len(native)%(FRAME*2):
        parser.error("PCM buffers differ or frame alignment invalid")
    tools=trace_tools(aac,decoder)
    rows=[]
    counts=Counter()
    for n in range(len(native)//(FRAME*2)):
        entry=tools.get(n,{})
        if entry.get("status")!="decoded":
            parser.error("frame %d missing decoded metadata"%n)
        scores=frame_scores(native,ref,n)
        pns=entry.get("pns_left",0)+entry.get("pns_right",0)
        category="PNS" if pns else "no-PNS"
        counts[category]+=1
        rows.append({"frame":n,"time_s":n*FRAME/(int(args.case.split("_")[1])),
                     **entry,"left":scores[0],"right":scores[1],
                     "worst_nrmse":max(s["nrmse"] for s in scores)})
    ordered=sorted(rows,key=lambda x:x["worst_nrmse"],reverse=True)
    print("AAC PNS frame diagnostics: %s; %d frames"%(args.case,len(rows)))
    print("PNS frames=%d non-PNS frames=%d"%(counts["PNS"],counts["no-PNS"]))
    for category in ("PNS","no-PNS"):
        group=[r for r in rows if (("PNS" if (r.get("pns_left",0)+r.get("pns_right",0)) else "no-PNS")==category)]
        if group:
            print("%s: max frame nRMSE=%.6f, mean frame nRMSE=%.6f"%
                  (category,max(r["worst_nrmse"] for r in group),
                   sum(r["worst_nrmse"] for r in group)/len(group)))
    print("Worst frames (diagnostic only; NOT a frame-level pass/fail gate):")
    for row in ordered[:max(1,args.top)]:
        print("frame=%d t=%.3fs nRMSE L=%.6f R=%.6f PNS=%d/%d "
              "TNS=%d MS=%d intensity=%d window=%s/%s RNG=%s->%s"%
              (row["frame"],row["time_s"],row["left"]["nrmse"],
               row["right"]["nrmse"],row.get("pns_left",0),
               row.get("pns_right",0),row.get("tns",0),row.get("ms",0),
               row.get("intensity",0),row.get("window_left","?"),
               row.get("window_right","?"),row.get("rng_before","?"),
               row.get("rng_after","?")))
    output=base.with_suffix(".pns-diagnostic.json")
    output.write_text(json.dumps({"case":args.case,"frames":rows},indent=2))
    print("Detailed frame data: %s"%output)
    return 0

if __name__=="__main__":
    sys.exit(main())
