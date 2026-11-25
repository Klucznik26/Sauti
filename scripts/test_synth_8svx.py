#!/usr/bin/env python3
"""Synthetic Amiga 8SVX files (PCM, Fibonacci delta, exponential delta; mono / stereo; extra chunks before and after BODY, odd sizes, truncated BODY, CHAN after the data) decoded by
MaktabaSauti (build/maktabasauti_cli) and by FFmpeg (8svx_fib / 8svx_exp / pcm_s8): the 16-bit PCM must be IDENTICAL.  The files are made here (no real Fibonacci file is publicly available);
FFmpeg is the reference for PCM files and for which files are accepted / how long they are; for Fibonacci / exponential files it is NOT the reference of the sample values: FFmpeg takes the LOW nibble first and
CLIPS, but real files (written by wavepak on an Amiga, tests/data/svx_wavepak_*) take the HIGH nibble first and the value WRAPS modulo 256 (the EA reference code stores into a byte).  The values of those files are
checked against the small model below (validated bit for bit against wavepak's own decoder: docs/IFF_8SVX.md); initial value in the second byte, stereo = whole left block then whole right block.
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
FIB = [-34, -21, -13, -8, -5, -3, -2, -1, 0, 1, 2, 3, 5, 8, 13, 21]; EXP = [-128, -64, -32, -16, -8, -4, -2, -1, 0, 1, 2, 4, 8, 16, 32, 64]
def model(data):
    """Expected 16-bit interleaved PCM of a complete (mono or stereo) compressed file; None when the file is not one this model covers."""
    comp = nch = None; body = None; at = 12
    while at + 8 <= len(data):
        tag = data[at:at + 4]; n = struct.unpack('>I', data[at + 4:at + 8])[0]; blob = data[at + 8:at + 8 + n]
        if tag == b'VHDR' and len(blob) >= 16: comp = blob[15]
        elif tag == b'CHAN' and len(blob) >= 4: nch = 2 if struct.unpack('>I', blob[:4])[0] == 6 else 1
        elif tag == b'BODY': body = blob
        at += 8 + n + (n & 1)
    if comp not in (1, 2) or body is None: return None
    nch = nch or 1; table = FIB if comp == 1 else EXP
    blocks = [body[:len(body) // 2], body[len(body) // 2:]] if nch == 2 else [body]
    chans = []
    for b in blocks:
        if len(b) < 2: return None
        y = b[1] - 256 if b[1] > 127 else b[1]; out = []
        for c in b[2:]:
            for nib in (c >> 4, c & 15):
                y = (y + table[nib] + 128) % 256 - 128; out.append(y)
        chans.append(out)
    m = min(len(c) for c in chans)
    return np.array([chans[k][i] * 256 for i in range(m) for k in range(nch)], dtype=np.int64)
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
            if data[35] in (1, 2) and not cut_stereo:
                mdl = model(data)
                if mdl is not None and len(mdl) == len(r): r = mdl                       # same length as FFmpeg, values from the model (high nibble first, wrap)
            same = len(q) == len(r) and (q == r).all()
            if same: ok += 1
            else:
                bad += 1; print('f%d: FAIL %d values vs ffmpeg %d, differing %d' % (i, len(q), len(r), int((q[:min(len(q), len(r))] != r[:min(len(q), len(r))]).sum())))
    finally:
        shutil.rmtree(w, ignore_errors=True)
    print('%d identical to the reference (FFmpeg for PCM, the model for Fibonacci / exponential), %d both reject, %d only FFmpeg rejects (cut metadata chunk), %d truncated stereo skipped, %d failures' % (ok, both_reject, ff_only_reject, skipped, bad))
    return 1 if bad else 0
if __name__ == '__main__':
    sys.exit(main())
