#!/usr/bin/env python3
"""Synthetic Amiga 8SVX files (PCM, Fibonacci delta, exponential delta; mono / stereo; extra chunks before and after BODY, odd sizes, truncated BODY, CHAN after the data) decoded by
MaktabaSauti (build/maktabasauti_cli) and by FFmpeg (8svx_fib / 8svx_exp / pcm_s8): the 16-bit PCM must be IDENTICAL.  The files are made here (no real Fibonacci file is publicly available);
FFmpeg is the independent reference for the layout (low nibble first, initial value in the second byte, clipping, stereo = whole left block then whole right block).
usage: scripts/test_synth_8svx.py [N]   (default 60 files; TMPDIR for scratch files).  No ffmpeg = SKIP (exit 0, said loudly: that is NOT a proof)."""
import os, random, shutil, struct, subprocess, sys, tempfile
import numpy as np
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CLI = os.path.join(ROOT, 'build', 'maktabasauti_cli')

def ck(tag, data):
    return tag + struct.pack('>I', len(data)) + data + (b'\0' if len(data) & 1 else b'')
def make(rng):
    comp = rng.choice([0, 1, 2]); nch = rng.choice([1, 1, 2]); rate = rng.choice([8000, 11025, 22050, 22095, 44100])
    per = rng.choice([3, 4, 5, 17, 100, 1000, 5000, 9001, 20000]) if comp else rng.choice([1, 2, 3, 17, 100, 5000, 9001])
    blocks = []
    for c in range(nch):
        data = bytes(rng.randrange(256) for _ in range(per))
        if comp: data = bytes([rng.randrange(256), rng.randrange(256)]) + data       # padding byte, initial value, codes
        blocks.append(data)
    body = b''.join(blocks)
    vh = struct.pack('>IIIHBBI', per, 0, 32, rate, 1, comp, 65536)
    parts = [ck(b'VHDR', vh)]
    chan = ck(b'CHAN', struct.pack('>I', 6 if nch == 2 else rng.choice([2, 4]))) if (nch == 2 or rng.random() < 0.3) else b''
    pre = rng.random() < 0.5
    if pre: parts.append(chan)
    if rng.random() < 0.5: parts.append(ck(b'NAME', b'x' * rng.randrange(0, 700)))
    if rng.random() < 0.3: parts.append(ck(b'ANNO', b'anno'))
    parts.append(ck(b'BODY', body))
    if not pre: parts.append(chan)
    if rng.random() < 0.3: parts.append(ck(b'(c) ', b'copyright'))
    f = b'FORM' + struct.pack('>I', 4 + sum(len(p) for p in parts)) + b'8SVX' + b''.join(parts)
    cut = False
    if rng.random() < 0.15 and len(f) > 80: f = f[:len(f) - rng.randrange(1, min(len(f) - 60, 200))]; cut = True     # truncated file
    return f, cut and nch == 2
def ffmpeg_pcm(path):
    out = subprocess.run(['ffmpeg', '-v', 'error', '-i', path, '-f', 's16le', '-acodec', 'pcm_s16le', '-'], capture_output=True, timeout=60)
    if out.returncode != 0: return None
    pr = subprocess.run(['ffprobe', '-v', 'error', '-select_streams', 'a:0', '-show_entries', 'stream=channels', '-of', 'csv=p=0', path], capture_output=True, text=True)
    return int(pr.stdout.strip()), np.frombuffer(out.stdout, dtype='<i2')
def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 60
    if not shutil.which('ffmpeg') or not shutil.which('ffprobe') or not os.path.exists(CLI):
        print('SKIP: no ffmpeg / maktabasauti_cli (this is NOT a proof of agreement)'); return 0
    w = tempfile.mkdtemp(prefix='sauti-8svx.', dir=os.environ.get('TMPDIR') or None); rng = random.Random(8); bad = 0; ok = 0; both_reject = 0; skipped = 0; ff_only_reject = 0
    try:
        for i in range(n):
            p = os.path.join(w, 'f%d.8svx' % i); o = os.path.join(w, 'o.sau')
            data, cut_stereo = make(rng)
            open(p, 'wb').write(data)
            if os.path.exists(o): os.remove(o)
            ref = ffmpeg_pcm(p)
            if cut_stereo: skipped += 1; continue          # a TRUNCATED stereo file: FFmpeg splits the bytes that are there in halves (the right channel then starts at the wrong place); we use the declared size, so the right channel keeps its place
            run = subprocess.run([CLI, p, o], capture_output=True, text=True, timeout=120)
            trunc = run.returncode != 0 and 'ended in the middle' in (run.stderr + run.stdout) and os.path.exists(o)
            ours = np.fromfile(o, dtype='<f4') if os.path.exists(o) and (run.returncode == 0 or trunc) else None
            if ref is None and ours is None: both_reject += 1; continue
            if ref is None: ff_only_reject += 1; continue          # FFmpeg refuses some files with a cut-off metadata chunk (metadata allocation), the data are fine
            if ours is None: print('f%d: FAIL ours rejects (rc %d) a file FFmpeg decodes (%d values): %s' % (i, run.returncode, len(ref[1]), (run.stderr or run.stdout).strip().splitlines()[-1:])); bad += 1; continue
            q = np.rint(ours.astype(np.float64) * 32768.0).astype(np.int64); r = ref[1].astype(np.int64)
            same = len(q) == len(r) and (q == r).all()
            if same: ok += 1
            else:
                bad += 1; print('f%d: FAIL %d values vs ffmpeg %d, differing %d' % (i, len(q), len(r), int((q[:min(len(q), len(r))] != r[:min(len(q), len(r))]).sum())))
    finally:
        shutil.rmtree(w, ignore_errors=True)
    print('%d identical to FFmpeg, %d both reject, %d only FFmpeg rejects (cut metadata chunk), %d truncated stereo skipped, %d failures' % (ok, both_reject, ff_only_reject, skipped, bad))
    return 1 if bad else 0
if __name__ == '__main__':
    sys.exit(main())
