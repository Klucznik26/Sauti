#!/usr/bin/env python3
"""PS-5a: independent FFmpeg reference comparison for Sauti HE-AAC v2.
Only the REFERENCE uses FFmpeg; Sauti native decoder runs separately.
Output is diagnostic, not a normative conformance certificate.
Requires numpy and ffmpeg in PATH. """
import argparse
import math
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

def run(cmd):
    p = subprocess.run(cmd, capture_output=True, text=True, check=False)
    if p.returncode:
        raise RuntimeError(f"{' '.join(map(str,cmd))}: exit={p.returncode}\n{p.stderr[-3500:]}")
    return p

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("input",type=Path,help="independently encoded ADTS HE-AAC v2")
    ap.add_argument("--native",default="./build/sauti_he_ps_decode")
    ap.add_argument("--pcm",type=Path,default=None,
                    help="compare an existing experimental interleaved stereo f32le file; do not run native decoder")
    ap.add_argument("--rate",type=int,default=48000,
                    help="HE-AAC output sample rate (default: 48000)")
    ap.add_argument("--search",type=int,default=8192,
                    help="maximum independent alignment lag in stereo sample pairs")
    a=ap.parse_args()
    if a.rate<=0 or a.search<0:ap.error("invalid rate/search")
    if not a.input.is_file():ap.error("input ADTS file missing")
    if not shutil.which("ffmpeg"):ap.error("ffmpeg executable missing")
    if a.pcm is not None and not a.pcm.is_file():ap.error("existing PCM preview file missing")
    try:
        import numpy as np
    except ImportError:
        ap.error("Install python3-numpy to compute the comparison")

    with tempfile.TemporaryDirectory(prefix="sauti_ps5_") as tmp:
        native=Path(tmp)/"sauti.f32le"
        ref=Path(tmp)/"ffmpeg.f32le"
        decoder=None
        if a.pcm is None:
            decoder=run([a.native,str(a.input),str(native)])
        else:
            native=a.pcm
        reference=run(["ffmpeg","-hide_banner","-nostdin","-loglevel","error",
                       "-i",str(a.input),"-map","0:a:0",
                       "-ac","2","-ar",str(a.rate),"-f","f32le",
                       "-c:a","pcm_f32le",str(ref),"-y"])
        native_pcm=np.fromfile(native,dtype="<f4")
        reference_pcm=np.fromfile(ref,dtype="<f4")
        if native_pcm.size%2 or reference_pcm.size%2 or not native_pcm.size or not reference_pcm.size:
            raise RuntimeError("empty or non-stereo PCM output")
        x=native_pcm.reshape((-1,2)).astype(np.float64)
        y=reference_pcm.reshape((-1,2)).astype(np.float64)
        if not np.isfinite(x).all() or not np.isfinite(y).all():
            raise RuntimeError("NaN or infinity in PCM")
        # Search over realistic decoder priming delays. Lag is relative
        # reference index for native[0]. Correlation removes gain
        # differences; the final metrics retain the original gain.
        def segment(lag):
            start_x=max(0,-lag)
            start_y=max(0,lag)
            n=min(x.shape[0]-start_x,y.shape[0]-start_y)
            return start_x,start_y,n
        def corr(lag):
            sx,sy,n=segment(lag)
            if n<1024:return float("-inf")
            # Fixed-size window avoids preferring smaller overlaps.
            n=min(n,8192)
            a1=x[sx:sx+n].reshape(-1)
            b1=y[sy:sy+n].reshape(-1)
            den=np.linalg.norm(a1)*np.linalg.norm(b1)
            if den<1e-12:return float("-inf")
            return float(np.dot(a1,b1)/den)
        coarse=range(-a.search,a.search+1,64)
        center=max(coarse,key=corr)
        best=max(range(max(-a.search,center-63),min(a.search,center+63)+1),key=corr)
        sx,sy,n=segment(best)
        if n<1024:raise RuntimeError("Not enough aligned stereo output")
        aa=x[sx:sx+n];bb=y[sy:sy+n]
        for name,audio in (("native",aa),("ffmpeg",bb)):
            rms=np.sqrt(np.mean(audio*audio,axis=0))
            peak=np.max(np.abs(audio),axis=0)
            print(f"{name}: frames={audio.shape[0]} RMS_L/R={rms[0]:.7g}/{rms[1]:.7g} "
                  f"peak_L/R={peak[0]:.7g}/{peak[1]:.7g}")
        diff=aa-bb
        err=np.sqrt(np.mean(diff*diff,axis=0))
        channel_corr=[]
        for k in (0,1):
            u=aa[:,k]-np.mean(aa[:,k])
            v=bb[:,k]-np.mean(bb[:,k])
            denom=np.linalg.norm(u)*np.linalg.norm(v)
            channel_corr.append(float(np.dot(u,v)/denom) if denom>1e-12 else float("nan"))
        print(f"alignment_lag={best} pairs ({1000*best/a.rate:.3f} ms), "
              f"global_corr={corr(best):.6f}, aligned_pairs={n}")
        print(f"RMSE_L/R={err[0]:.7g}/{err[1]:.7g}, "
              f"channel_corr_L/R={channel_corr[0]:.6f}/{channel_corr[1]:.6f}")
        print(f"native_pairs={len(x)} ref_pairs={len(y)} "
              f"(native intentionally retains final pending 2048 pairs)")
        # Tone isolation is a diagnostic for the independent dual-tone
        # FDK fixture: L=440 Hz, R=880 Hz. Do not interpret as a generic
        # music-codec quality score; keep one stable 1-second window.
        def tone_audit(name,audio):
            n0=min(audio.shape[0],a.rate)
            if n0<2048:
                print(f"{name}: insufficient length for tone audit")
                return
            w=np.hanning(n0)
            freqs=np.fft.rfftfreq(n0,1.0/a.rate)
            spectra=np.abs(np.fft.rfft(audio[:n0]*w[:,None],axis=0))**2
            audible=(freqs>=200.0)&(freqs<=2000.0)
            def power(f,channel):
                near=np.abs(freqs-f)<=max(3.0,2.0*a.rate/n0)
                return float(spectra[near,channel].sum())
            vals=[power(440.0,0),power(880.0,0),
                  power(440.0,1),power(880.0,1)]
            total=spectra[audible].sum(axis=0)
            eps=1e-30
            purity_l=vals[0]/max(float(total[0]),eps)
            purity_r=vals[3]/max(float(total[1]),eps)
            sep_l=10*math.log10((vals[0]+eps)/(vals[2]+eps))
            sep_r=10*math.log10((vals[3]+eps)/(vals[1]+eps))
            print(f"{name} tones: L_440_fraction={purity_l:.4f} "
                  f"R_880_fraction={purity_r:.4f}, "
                  f"L440_over_R440={sep_l:+.1f} dB "
                  f"R880_over_L880={sep_r:+.1f} dB")
        tone_audit("native",aa)
        tone_audit("ffmpeg",bb)
        # One aligned one-second snapshot across the FULL audible spectrum.
        # Tone purity above intentionally examines only 200..2000 Hz,
        # so it can remain excellent despite enormous HF energy.
        # Windowing suppresses artificial leakage of the test sine tones.
        def band_audit(native_audio, reference_audio):
            count=min(a.rate,len(native_audio),len(reference_audio))
            if count<2048:
                return
            win=np.hanning(count)[:,None]
            freq=np.fft.rfftfreq(count,d=1.0/a.rate)
            spec_native=np.abs(np.fft.rfft(native_audio[:count]*win,axis=0))**2
            spec_ref=np.abs(np.fft.rfft(reference_audio[:count]*win,axis=0))**2
            spec_error=np.abs(np.fft.rfft(
                (native_audio[:count]-reference_audio[:count])*win,axis=0))**2
            full=spec_native.sum(axis=0)
            eps=1e-30
            bands=((0,2000),(2000,6000),(6000,12000),(12000,a.rate/2))
            for lo,hi in bands:
                if lo>=hi:continue
                region=(freq>=lo)&(freq<(hi if hi<a.rate/2 else hi+0.001))
                powers=[v[region].sum(axis=0) for v in
                        (spec_native,spec_ref,spec_error)]
                native_power,reference_power,error_power=powers
                ratio_db=10*np.log10((native_power+eps)/(reference_power+eps))
                err_db=10*np.log10((error_power+eps)/(reference_power+eps))
                fraction=native_power/(full+eps)
                print(f"SPECTRUM {lo/1000:.0f}-{hi/1000:.0f}kHz: "
                      f"native_share_L/R={fraction[0]:.5f}/{fraction[1]:.5f} "
                      f"native_vs_ref_L/R={ratio_db[0]:+.1f}/{ratio_db[1]:+.1f}dB "
                      f"error_vs_ref_L/R={err_db[0]:+.1f}/{err_db[1]:+.1f}dB")
        band_audit(aa,bb)
        print("DIAGNOSTIC ONLY — check fidelity and SBR/PS timing; this is not a compliance PASS")
        if decoder is not None:
            print(decoder.stderr.strip())
        else:
            print("Native input: existing experimental PCM preview (no decoder invoked)")
    return 0

if __name__=="__main__":
    try:sys.exit(main())
    except (RuntimeError,OSError) as exc:
        print(f"FAIL PS-5a: {exc}",file=sys.stderr)
        sys.exit(1)
