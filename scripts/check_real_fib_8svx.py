#!/usr/bin/env python3
"""Real Fibonacci-delta 8SVX files (written by AudioMasterIV on an Amiga, 1997; NostalgicPlayer 'IFF-8SVX (Fibonacci)': Lille/Stor . Mono/Stereo) against the Sauti decoder:
  1. build/maktabasauti_cli output == the model (HIGH nibble first, value wraps modulo 256), sample for sample;
  2. statistics that tell the nibble order apart on real music: lag-1 autocorrelation and, for stereo, L/R correlation must be HIGHER with the high nibble first (they are, in all four files).
The files are third-party (licence unknown) and are NOT in the repository.  usage: scripts/check_real_fib_8svx.py DIR   (DIR holds the four .8svx files, e.g. ~/Pobrane/'IFF-8SVX (Fibonacci)' unpacked)"""
import glob, os, struct, subprocess, sys
import numpy as np
FIB = [-34, -21, -13, -8, -5, -3, -2, -1, 0, 1, 2, 3, 5, 8, 13, 21]
CLI = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'build', 'maktabasauti_cli')
def parse(p):
    d = open(p, 'rb').read(); i = 12; stereo = False; body = b''; comp = None
    while i + 8 <= len(d):
        t = d[i:i + 4]; n = struct.unpack('>I', d[i + 4:i + 8])[0]; blob = d[i + 8:i + 8 + n]
        if t == b'VHDR': comp = blob[15]
        if t == b'CHAN': stereo = struct.unpack('>I', blob[:4])[0] == 6
        if t == b'BODY': body = blob
        i += 8 + n + (n & 1)
    return comp, stereo, body
def dec(b, hi):
    y = b[1] - 256 if b[1] > 127 else b[1]; out = []
    for c in b[2:]:
        for nib in ((c >> 4, c & 15) if hi else (c & 15, c >> 4)): y = (y + FIB[nib] + 128) % 256 - 128; out.append(y)
    return np.array(out, float)
bad = 0; files = sorted(glob.glob(os.path.join(sys.argv[1], '*.8svx')))
if not files: print('SKIP: no .8svx files in', sys.argv[1]); sys.exit(0)
for p in files:
    comp, stereo, body = parse(p)
    if comp != 1: continue
    blocks = [body[:len(body) // 2], body[len(body) // 2:]] if stereo else [body]
    subprocess.run([CLI, p, '/tmp/real_fib.f32'], capture_output=True)
    ours = np.round(np.fromfile('/tmp/real_fib.f32', '<f4') * 128).astype(int)
    want = np.stack([dec(b, True) for b in blocks], 1).reshape(-1).astype(int); same = len(ours) == len(want) and bool((ours == want).all())
    a1 = {hi: np.corrcoef(dec(blocks[0], hi)[:-1], dec(blocks[0], hi)[1:])[0, 1] for hi in (True, False)}
    msg = 'lag-1 autocorr high %.4f vs low %.4f' % (a1[True], a1[False]); ok = same and a1[True] >= a1[False]
    if stereo:
        c = {hi: np.corrcoef(dec(blocks[0], hi), dec(blocks[1], hi))[0, 1] for hi in (True, False)}; msg += ', L/R corr high %.4f vs low %.4f' % (c[True], c[False]); ok = ok and c[True] >= c[False]
    print('%-20s %s: %s' % (os.path.basename(p), 'OK  ' if ok else 'FAIL', ('ours == model; ' if same else 'ours DIFFERS from model; ') + msg)); bad += not ok
sys.exit(1 if bad else 0)
