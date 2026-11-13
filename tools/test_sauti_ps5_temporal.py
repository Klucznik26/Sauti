#!/usr/bin/env python3
"""PS-5b: real ADTS bitstream temporal SBR side-data audit smoke."""
import pathlib
import subprocess
import sys
import tempfile

def main():
    if len(sys.argv)!=3:
        print("usage: test_sauti_ps5_temporal.py GENERATOR AUDITOR",file=sys.stderr)
        return 2
    with tempfile.TemporaryDirectory(prefix="sauti_ps5_temporal_") as td:
        fixture=pathlib.Path(td)/"three_ps.adts"
        subprocess.run([sys.argv[1],"--emit-ps-smoke",str(fixture)],check=True)
        good=subprocess.run([sys.argv[2],str(fixture)],capture_output=True,text=True)
        if good.returncode or "PASS PS-5b side-data audit frames=3" not in good.stderr:
            raise AssertionError("positive PS temporal audit failed: "+good.stderr)
        preview=pathlib.Path(td)/"preview.f32le"
        listen=subprocess.run([sys.argv[2],str(fixture),
            "--experimental-pcm",str(preview)],
            capture_output=True,text=True)
        if listen.returncode or not preview.exists() or preview.stat().st_size!=2*4096*4:
            raise AssertionError("experimental PCM path failed: "+listen.stderr)
        for mode in ("--experimental-pcm-no-noise","--experimental-pcm-unscaled","--experimental-pcm-noise-25","--experimental-pcm-noise-50","--experimental-pcm-noise-75","--experimental-pcm-raw-hf","--experimental-pcm-unit-scale","--experimental-pcm-low-only"):
            mode_file=pathlib.Path(td)/(mode[2:]+".f32le")
            trial=subprocess.run([sys.argv[2],str(fixture),mode,str(mode_file)],
                capture_output=True,text=True)
            if trial.returncode or not mode_file.exists() or mode_file.stat().st_size!=2*4096*4:
                raise AssertionError("PCM isolation mode "+mode+" failed: "+trial.stderr)
        # Full and historical unit-scale are intentionally identical
        # until normative limiter/smoothing replaces this preview.
        unit_file=pathlib.Path(td)/"experimental-pcm-unit-scale.f32le"
        if preview.read_bytes()!=unit_file.read_bytes():
            raise AssertionError("normalized full differs from unit-scale compatibility")
        bad=pathlib.Path(td)/"truncated.adts"
        bad.write_bytes(fixture.read_bytes()[:-1])
        negative=subprocess.run([sys.argv[2],str(bad)],capture_output=True,text=True)
        incomplete=pathlib.Path(td)/"bad_preview.f32le"
        rejected=subprocess.run([sys.argv[2],str(bad),
            "--experimental-pcm",str(incomplete)],
            capture_output=True,text=True)
        if rejected.returncode==0 or incomplete.exists():
            raise AssertionError("experimental PCM failed atomic cleanup on truncated ADTS")
        if negative.returncode==0:
            raise AssertionError("truncated frame was accepted")
    print("PASS PS temporal: ADTS parse, experimental stereo isolation modes and truncated cleanup")
    return 0

if __name__=="__main__":
    sys.exit(main())
