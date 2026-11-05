#!/usr/bin/env python3
"""SBR-54w II: smoke-test actual compiled C QMF exporter and manifest."""
import array
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile

def write_f32(path, values):
    a = array.array("f", values)
    if sys.byteorder != "little":
        a.byteswap()
    path.write_bytes(a.tobytes())

def main():
    if len(sys.argv)!=2:
        raise SystemExit("usage: test_sauti_qmf_low_export.py EXECUTABLE")
    exe=Path(sys.argv[1]).resolve()
    wrapper=Path(__file__).with_name("sauti_qmf_low_manifest.py")
    audit=Path(__file__).with_name("sauti_he_aac_qmf_reference_audit.py")
    with tempfile.TemporaryDirectory(prefix="sbr54w-") as tmp:
        d=Path(tmp)
        pcm=d/"core.f32le"
        qmf=d/"out.qmf.f32le"
        manifest=d/"out.json"
        # Include tonal and broadband components, identical on both frames.
        values=[0.1*math.sin(i*0.17)+0.04*math.cos(i*0.023)
                for i in range(2048)]
        write_f32(pcm, values)
        subprocess.run([str(exe),str(pcm),str(qmf)],check=True,
                       capture_output=True,text=True)
        subprocess.run([sys.executable,str(wrapper),"--core-rate","24000",
                        str(pcm),str(qmf),str(manifest)],check=True,
                       capture_output=True,text=True)
        obj=json.loads(manifest.read_text())
        assert obj["frame_count"]==2 and not obj["independent"]
        assert qmf.stat().st_size==2*32*4*2*4
        a=array.array("f")
        a.frombytes(qmf.read_bytes())
        if sys.byteorder!="little":
            a.byteswap()
        assert all(x==0.0 for x in a[:6*4*2])
        assert any(abs(x)>1e-12 for x in a[6*4*2:])
        # The exporter must be stateful: frame 1 source slots 0..5
        # equal frame 0 RAW analysis slots 26..31, hence not all zero.
        assert any(abs(x)>1e-12 for x in a[32*4*2:38*4*2])
        bad=d/"truncated.f32le"
        bad.write_bytes(pcm.read_bytes()[:-4])
        rejected=subprocess.run([str(exe),str(bad),str(d/"bad.qmf")],
                                capture_output=True,text=True)
        assert rejected.returncode!=0
        subprocess.run([sys.executable,str(audit),"--self-test"],
                       check=True,capture_output=True)
    print("PASS SBR-54w II Sauti source QMF export, carry, preroll, SHA manifest and truncated input")

if __name__=="__main__":
    main()
