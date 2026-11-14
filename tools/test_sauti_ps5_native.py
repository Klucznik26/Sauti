#!/usr/bin/env python3
"""PS-5a native CLI smoke; generator fixture, not codec conformance."""
import os
import pathlib
import struct
import subprocess
import sys
import tempfile

def main():
    if len(sys.argv)!=3:
        print("Usage: test_sauti_ps5_native.py TEST_GENERATOR NATIVE_DECODER",file=sys.stderr)
        return 2
    generator,decoder=sys.argv[1:]
    with tempfile.TemporaryDirectory(prefix="sauti_ps5_smoke_") as td:
        adts=pathlib.Path(td)/"positive.adts"
        output=pathlib.Path(td)/"positive.f32le"
        subprocess.run([generator,"--emit-ps-smoke",str(adts)],check=True)
        subprocess.run([decoder,str(adts),str(output)],check=True)
        raw=output.read_bytes()
        if len(raw)!=2*4096*4:
            raise AssertionError(f"expected two delayed PCM frames, got {len(raw)} bytes")
        pcm=struct.unpack("<8192f",raw)
        if not all(abs(x)<float("inf") for x in pcm):
            raise AssertionError("nonfinite output")
        # Strict failure must remove incomplete file; do not treat
        # unsupported or truncated stream as partial successful decode.
        damaged=pathlib.Path(td)/"truncated.adts"
        damaged.write_bytes(adts.read_bytes()[:-1])
        partial=pathlib.Path(td)/"partial.f32le"
        result=subprocess.run([decoder,str(damaged),str(partial)],
                              capture_output=True,text=True)
        if result.returncode==0 or partial.exists():
            raise AssertionError("truncated ADTS did not fail closed")
    print("PASS AAC PS-5a: native CLI two delayed stereo PCM frames; truncated stream removes partial output")
    return 0

if __name__=="__main__":
    sys.exit(main())
