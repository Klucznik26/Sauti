#!/usr/bin/env python3
"""AAC SSR (object type 3) conformance streams of ISO/IEC 14496-26 (compressedMp4/as00..as17 x 12 sampling rates, 216 files; downloaded by the user from standards.iso.org, NOT in the repository):
every file must open as aac/SSR, decode without a single error to 1024 samples per access unit and channel, with finite samples and a plausible level, and the engine must agree sample for sample
with the plain lcn decoder (tools/sauti_ssr_probe) on the mono / stereo files.  The synthesis itself is checked against the ISO reference gain control tool in tests/test_aac_ssr.c.
usage: scripts/test_ssr_conformance.py DIR     (DIR holds the as*.mp4 files; SKIP, said loudly, exit 0 without them)"""
import glob, os, struct, subprocess, sys, tempfile
import numpy as np
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__))); CLI = os.path.join(ROOT, 'build', 'maktabasauti_cli'); PROBE = os.path.join(ROOT, 'build', 'sauti_ssr_probe')
files = sorted(glob.glob(os.path.join(sys.argv[1], 'as[0-9][0-9]_[0-9][0-9].mp4'))) if len(sys.argv) > 1 else []
if not files: print('SKIP: no as*.mp4 conformance streams (this is NOT a proof)'); sys.exit(0)
subprocess.run(['make', '-s', 'build/maktabasauti_cli', 'build/sauti_ssr_probe'], cwd=ROOT, check=True)
W = tempfile.mkdtemp(prefix='sauti-ssr.', dir=os.environ.get('TMPDIR') or None); bad = 0; compared = 0
for f in files:
    n = os.path.basename(f)[:-4]; out = os.path.join(W, n + '.f32')
    r = subprocess.run([CLI, f, out], capture_output=True, text=True); text = r.stdout + r.stderr
    if r.returncode or 'read:' in text or 'open:' in text or 'aac/SSR' not in text: print('%s: FAIL %s' % (n, text.strip().splitlines()[-1:])); bad += 1; continue
    ch = int(text.split('channels=')[1].split()[0]); a = np.fromfile(out, '<f4')
    subprocess.run(['python3', '-I', os.path.join(ROOT, 'tools', 'mp4_dump_aus.py'), f, os.path.join(W, n)], capture_output=True)
    aus = open(os.path.join(W, n + '.aus'), 'rb').read(); count = 0; p = 0
    while p + 4 <= len(aus): p += 4 + struct.unpack('>I', aus[p:p + 4])[0]; count += 1
    ok = len(a) == count * 1024 * ch and bool(np.isfinite(a).all()) and 1e-4 < float(a.std()) < 1.0
    if ok and ch <= 2:
        rate = {'08': 11, '11': 10, '12': 9, '16': 8, '22': 7, '24': 6, '32': 5, '44': 4, '48': 3, '64': 2, '88': 1, '96': 0}[n[-2:]]
        pr = subprocess.run([PROBE, os.path.join(W, n + '.aus'), str(rate), str(ch), os.path.join(W, n + '.probe.f32')], capture_output=True, text=True)
        b = np.fromfile(os.path.join(W, n + '.probe.f32'), '<f4'); ok = len(a) == len(b) and bool((a == b).all()); compared += 1
    if not ok: print('%s: FAIL (%d samples, %d access units, %d channels, rms %.4f)' % (n, len(a), count, ch, float(a.std()) if len(a) else 0)); bad += 1
print('%d streams, %d failures, %d compared with the lcn decoder' % (len(files), bad, compared)); sys.exit(1 if bad else 0)
