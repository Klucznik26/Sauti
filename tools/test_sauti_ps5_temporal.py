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
        bad=pathlib.Path(td)/"truncated.adts"
        bad.write_bytes(fixture.read_bytes()[:-1])
        negative=subprocess.run([sys.argv[2],str(bad)],capture_output=True,text=True)
        if negative.returncode==0:
            raise AssertionError("truncated frame was accepted")
    print("PASS PS-5b temporal side data: generated ADTS stream and truncated rollback")
    return 0

if __name__=="__main__":
    sys.exit(main())
