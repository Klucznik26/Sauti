#!/usr/bin/env python3
"""SBR-54x: four independent, single-tone QMF transfer measurements.

The 32-band QMF nominal spacing is fs/64 = 375 Hz at 24 kHz.
Use nominal band-center sines for LOW 2..5 (937.5..2062.5 Hz);
these are diagnostic excitation tones, not AAC-decoded PCM.
Runs Sauti standalone analyzer and separately built instrumented FFmpeg
against byte-identical PCM; validates manifests, raw exports and shape.
It NEVER modifies either decoder or the already built FFmpeg executable.
"""
import argparse
import array
import hashlib
import math
import os
from pathlib import Path
import subprocess
import sys

from sauti_he_aac_qmf_reference_audit import run as strict_compare
from sauti_sbr54w_gain_audit import evaluate as gain_compare

RATE = 24000
BANDS = (2, 3, 4, 5)
ROOT = Path(__file__).resolve().parent.parent


def run_command(args, env=None):
    subprocess.run([str(x) for x in args], check=True, env=env)


def pcm_tone(path, frames, freq, amplitude=0.12):
    import struct
    with path.open("wb") as f:
        for index in range(frames * 1024):
            sample = amplitude * math.sin(2.0 * math.pi * freq * index / RATE)
            f.write(struct.pack("<f", sample))
    return hashlib.sha256(path.read_bytes()).hexdigest()


def energy_profile(binary, frames):
    data = array.array("f")
    data.frombytes(binary.read_bytes())
    if sys.byteorder != "little":
        data.byteswap()
    energies = []
    for band in range(4):
        indices = (((frame*32+slot)*4+band)*2
                   for frame in range(frames)
                   for slot in range(6 if frame == 0 else 0, 32))
        energies.append(math.fsum(
            data[idx]**2+data[idx+1]**2 for idx in indices))
    total = math.fsum(energies)
    if not math.isfinite(total) or total <= 0:
        raise ValueError(f"invalid total QMF energy: {binary}")
    return [value/total for value in energies]


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--ffmpeg",default=str(Path.home()/"Sauti-tools/ffmpeg-sbr54w/ffmpeg"))
    p.add_argument("--fixture",default=str(Path.home()/"Sauti-fixtures/he_test.aac"))
    p.add_argument("--exporter",default=str(ROOT/"build/sauti_qmf_low_export"))
    p.add_argument("--output-dir",default="/tmp/sbr54x-tones")
    p.add_argument("--frames",type=int,default=50)
    args=p.parse_args()
    if not 2<=args.frames<=100:
        p.error("--frames must be 2..100")
    ffmpeg=Path(args.ffmpeg)
    fixture=Path(args.fixture)
    exporter=Path(args.exporter)
    for label,path in (("FFmpeg instrumented executable",ffmpeg),
                       ("HE-AAC fixture",fixture),("Sauti QMF exporter",exporter)):
        if not path.is_file():
            p.error(f"missing {label}: {path}")
    out=Path(args.output_dir)
    out.mkdir(parents=True,exist_ok=True)
    results=[]
    print("SBR-54x: independent single-tone QMF analysis (24 kHz; 32 bands)")
    print("Cross-band energy shares are normalized ONLY over LOW 2..5, not all 32 bands.")
    for target in BANDS:
        freq=(target+0.5)*RATE/64
        case=out/f"low{target}"
        case.mkdir(parents=True,exist_ok=True)
        pcm=case/"shared_core.f32le"
        sa=case/"sauti.qmf.f32le"
        ff=case/"ffmpeg.qmf.f32le"
        sam=case/"sauti.json"
        ffm=case/"ffmpeg.json"
        digest=pcm_tone(pcm,args.frames,freq)
        run_command([exporter,pcm,sa])
        run_command([sys.executable,ROOT/"tools/sauti_qmf_low_manifest.py",
                     "--core-rate",RATE,pcm,sa,sam])
        ff.unlink(missing_ok=True)
        env=os.environ.copy()
        env["SAUTI_SBR54W_CORE"]=str(pcm)
        env["SAUTI_SBR54W_QMF"]=str(ff)
        # Never append stderr to data: binary export remains separate.
        run_command([ffmpeg,"-hide_banner","-v","warning","-threads","1",
                     "-i",fixture,"-frames:a",str(args.frames),"-f","null","-"],env=env)
        run_command([sys.executable,ROOT/"tools/sauti_sbr54w_verify_reference.py",pcm,ff])
        run_command([sys.executable,ROOT/"tools/sauti_ffmpeg_qmf_manifest.py",
                     "--core-rate",RATE,pcm,ff,ffm])
        strict=strict_compare(sa,sam,ff,ffm)
        from sauti_he_aac_qmf_reference_audit import read_complex
        a=read_complex(sa,args.frames)
        b=read_complex(ff,args.frames)
        # Detect the former stereo-first-two-calls bug: its two initial
        # QMF frame sections were bit-identical for every tone. A genuine
        # single-channel continuation must preserve history across frames.
        first = b[6*8:32*8]
        second = b[(32+6)*8:64*8]
        if first == second:
            raise ValueError(
                f"LOW {target}: FFmpeg repeated startup frames byte-for-byte; "
                "check stereo channel selection and fixture"
            )
        fitted=gain_compare(a,b,args.frames)
        ea=energy_profile(sa,args.frames)
        eb=energy_profile(ff,args.frames)
        for band,fit in zip(BANDS,fitted):
            _,n,ratio,_,_,_,residual,coh,_=fit
            results.append((target,freq,band,ea[band-2],eb[band-2],
                            coh,residual,ratio,digest))
            print(f"EXCITE LOW={target} {freq:.1f}Hz | OBSERVE LOW={band} "
                  f"Sauti_share={ea[band-2]:.6f} FF_share={eb[band-2]:.6f} "
                  f"coherence={coh:.6f} scaled_residual={residual:.6f} "
                  f"FF/Sauti_RMS={ratio:.6g}",flush=True)
        assert len(strict)==4
    table=out/"report.tsv"
    table.write_text(
        "excited_low\tfreq_hz\tobserved_low\tsauti_energy_share\tffmpeg_energy_share"
        "\tcomplex_coherence\tscaled_residual\tff_over_sauti_rms\tcore_sha256\n"
        + "".join(f"{t}\t{freq:.1f}\t{b}\t{sa:.9g}\t{ff:.9g}\t{co:.9g}\t{re:.9g}\t{ratio:.9g}\t{digest}\n"
                  for t,freq,b,sa,ff,co,re,ratio,digest in results),
        encoding="utf-8")
    print(f"SBR-54x: all four independent tones completed, report={table}")
    print("NOTE: this is a diagnostic, not a production decoder PASS or QMF conformance proof.")


if __name__=="__main__":
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError) as exc:
        raise SystemExit(f"SBR-54x FAILED (no complete PASS): {exc}")
