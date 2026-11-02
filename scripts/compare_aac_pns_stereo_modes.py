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

def scan_frame_lags(native,reference,frames=(0,1,12,21,22),radius=16):
    """Diagnostic-only lag probe; strict comparison remains lag=0."""
    nframes=min(len(native),len(reference))//2048
    for frame in frames:
        if frame>=nframes:
            continue
        for channel,label in ((0,"L"),(1,"R")):
            anchor=frame*1024
            values=[]
            for lag in range(-radius,radius+1):
                lo=max(0,-lag)
                hi=min(1024,1024-lag)
                sqerr=sqref=0.0
                for t in range(lo,hi):
                    x=float(native[2*(anchor+t)+channel])
                    y=float(reference[2*(anchor+t+lag)+channel])
                    sqerr+=(x-y)**2
                    sqref+=y*y
                values.append((lag,(sqerr/max(sqref,1e-22))**0.5))
            best=min(values,key=lambda pair:pair[1])
            zero=values[radius][1]
            print("frame %d channel %s lag_scan: best=%+d nRMSE=%.6f "
                  "zero_lag=%.6f"%(frame,label,best[0],best[1],zero))

def residual_contributions(native,reference,ignore_initial=1):
    """Rank whole-frame squared-error contributions to the *strict* PCM metric.

    Frame zero is excluded, matching the strict comparator's overlap
    warmup. These diagnostics never omit frames from the actual gate.
    """
    nframes=min(len(native),len(reference))//2048
    for channel,label in ((0,"L"),(1,"R")):
        data=[]
        total_error=total_ref=0.0
        for frame in range(ignore_initial,nframes):
            start=frame*2048+channel
            finish=(frame+1)*2048
            sqerror=sqref=0.0
            for k in range(start,finish,2):
                a=float(native[k]);b=float(reference[k])
                sqerror+=(a-b)**2
                sqref+=b*b
            data.append((frame,sqerror,sqref))
            total_error+=sqerror
            total_ref+=sqref
        data.sort(key=lambda row:row[1],reverse=True)
        print("channel %s post-warmup: total_nRMSE=%.6f"%(label,
              (total_error/max(total_ref,1e-22))**0.5))
        for frame,err,ref in data[:8]:
            print("  error_contrib frame=%d share=%.2f%% "
                  "frame_nRMSE=%.5f"%(frame,100*err/max(total_error,1e-22),
                                    (err/max(ref,1e-22))**0.5))
        for dropped in (1,2,3):
            removed=data[:dropped]
            remaining_err=total_error-sum(v[1] for v in removed)
            remaining_ref=total_ref-sum(v[2] for v in removed)
            print("  diagnostic_only omit_top_%d_frames nRMSE=%.6f"%(
                dropped,(remaining_err/max(remaining_ref,1e-22))**0.5))

def stereo_residual_components(native,reference,frames=(1,2,12,13,21,22)):
    """Compare squared PCM residual energy in mid/side coordinates.

    This is a lossless change of coordinates for diagnostic residuals,
    NOT a modification of the AAC MS decoder or the strict PCM gate.
    """
    count=min(len(native),len(reference))//2048
    print("Stereo residual analysis (post-decode, not a conformance gate):")
    for frame in frames:
        if frame>=count:
            continue
        sum_mid=sum_side=cross=sum_l=sum_r=0.0
        for k in range(frame*2048,(frame+1)*2048,2):
            dl=float(native[k])-float(reference[k])
            dr=float(native[k+1])-float(reference[k+1])
            mid=(dl+dr)*0.5
            side=(dl-dr)*0.5
            sum_mid+=mid*mid
            sum_side+=side*side
            sum_l+=dl*dl
            sum_r+=dr*dr
            cross+=dl*dr
        total=sum_mid+sum_side
        corr=cross/(sum_l*sum_r)**0.5 if sum_l*sum_r>1e-25 else 0.0
        print("  frame=%d mid_error=%.2f%% side_error=%.2f%% "
              "LR_residual_corr=%+.5f"%(frame,
                  100*sum_mid/max(total,1e-25),
                  100*sum_side/max(total,1e-25),corr))

def overlap_region_residuals(native,reference,frames=(1,22)):
    """Diagnostic-only EIGHT_SHORT overlap attribution.

    Samples [0,448) are inherited exclusively from preceding overlap.
    Samples [448,1024) also contain current short IMDCT contributions.
    Strict verifier remains unchanged.
    """
    count=min(len(native),len(reference))//2048
    print("EIGHT_SHORT inherited-overlap vs current-window residuals:")
    for frame in frames:
        if frame>=count:
            continue
        for ch,label in ((0,"L"),(1,"R")):
            parts=[]
            for a,b,name in ((0,448,"inherited_only"),
                             (448,576,"first_short"),
                             (576,1024,"short_overlap")):
                error=energy=0.0
                for k in range(a,b):
                    i=frame*2048+2*k+ch
                    x=float(native[i]);y=float(reference[i])
                    error+=(x-y)**2
                    energy+=y*y
                parts.append((name,error,energy))
            total=sum(p[1] for p in parts)
            print("  frame=%d ch=%s"%(frame,label))
            for name,err,ref in parts:
                print("    region=%s error_share=%.3f%% "
                      "region_nRMSE=%.6f ref_energy=%.8g"%(
                      name,100*err/max(total,1e-22),
                      (err/max(ref,1e-22))**0.5,ref))

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
               "build/test_aac_real_stereo_windows_sine",
               "build/test_aac_real_stereo_short_first_current",
               "build/test_aac_real_stereo_short_direct_imdct",
               "build/test_aac_real_stereo_short_no_ms",
               "build/test_aac_real_stereo_short_no_intensity"])
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
                     ("sine_windows","build/test_aac_real_stereo_windows_sine"),
                     ("short_first_current","build/test_aac_real_stereo_short_first_current"),
                     ("short_direct_imdct","build/test_aac_real_stereo_short_direct_imdct"),
                     ("short_no_ms","build/test_aac_real_stereo_short_no_ms"),
                     ("short_no_intensity","build/test_aac_real_stereo_short_no_intensity")):
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
    for variant in ("independent","positive","zero","sine_windows","short_first_current","short_direct_imdct","short_no_ms","short_no_intensity"):
        other=(out/(args.case+"."+variant+".f32")).read_bytes()
        print("PCM bit-identical current vs %s: %s"%(variant,baseline==other))
    current_pcm=read_pcm(out/(args.case+".current.f32"))
    reference_pcm=read_pcm(reference)
    inspect_transient_blocks(current_pcm,reference_pcm)
    scan_frame_lags(current_pcm,reference_pcm)
    residual_contributions(current_pcm,reference_pcm)
    stereo_residual_components(current_pcm,reference_pcm)
    overlap_region_residuals(current_pcm,reference_pcm)
    print("Strict gate (unchanged): "+", ".join(
        "%s=%s"%(key,value.get("ok")) for key,value in scores.items()))
    (out/(args.case+".json")).write_text(json.dumps(scores,indent=2))
    print("Diagnostic report: %s"%(out/(args.case+".json")))
    return 0

if __name__=="__main__":
    sys.exit(main())
