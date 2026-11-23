#!/usr/bin/env python3
"""Decodes REAL G.711 files (mu-law / A-law in WAV, WAVE_FORMAT_EXTENSIBLE, AIFF-C and Sun AU: e.g. the public AFsp / CCRMA / GoldWave samples of McGill University kept OUTSIDE the repository, untrusted data)
with MaktabaSauti (build/maktabasauti_cli) and compares the PCM with FFmpeg (the independent reference: its own G.711 tables).  G.711 decodes to 16-bit integers, so after rounding ours to 16 bits the
two must be IDENTICAL (0 differing samples); the rate, channel count and length must agree too.  The files are only opened by the decoders (arguments are passed as lists, never through a shell).
usage: scripts/test_real_g711.py DIR   (default ~/Sauti-tools/real-files/g711/dl; TMPDIR for the scratch files).  No directory / no ffmpeg = SKIP (exit 0, said loudly: that is NOT a proof)."""
import os, shutil, subprocess, sys, tempfile
import numpy as np
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CLI = os.path.join(ROOT, 'build', 'maktabasauti_cli')
# the files whose header is deliberately broken or exotic: only checked to not crash, with the expected result of the decoder
EXPECT_REJECT = set()

def ffmpeg_pcm(path):
    # -f s16le of the first stream; the channel count and rate come from ffprobe
    pr = subprocess.run(['ffprobe', '-v', 'error', '-select_streams', 'a:0', '-show_entries', 'stream=sample_rate,channels', '-of', 'csv=p=0', path], capture_output=True, text=True, timeout=60)
    if pr.returncode != 0 or not pr.stdout.strip(): return None
    rate, ch = [int(x) for x in pr.stdout.strip().split(',')[:2]]
    out = subprocess.run(['ffmpeg', '-v', 'error', '-i', path, '-f', 's16le', '-acodec', 'pcm_s16le', '-'], capture_output=True, timeout=120)
    if out.returncode != 0: return None
    return rate, ch, np.frombuffer(out.stdout, dtype='<i2')

def main():
    d = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser('~/Sauti-tools/real-files/g711/dl')
    if not os.path.isdir(d) or not shutil.which('ffmpeg') or not shutil.which('ffprobe') or not os.path.exists(CLI):
        print('SKIP: no files / ffmpeg / maktabasauti_cli (this is NOT a proof of agreement)'); return 0
    w = tempfile.mkdtemp(prefix='sauti-g711.', dir=os.environ.get('TMPDIR') or None)
    bad = 0; n_ok = 0
    try:
        for f in sorted(os.listdir(d)):
            p = os.path.join(d, f)
            if not os.path.isfile(p): continue
            ref = ffmpeg_pcm(p)
            o = os.path.join(w, 'o.sau')
            if os.path.exists(o): os.remove(o)
            run = subprocess.run([CLI, p, o], capture_output=True, text=True, timeout=300)
            trunc = run.returncode != 0 and 'ended in the middle' in (run.stderr + run.stdout) and os.path.exists(o)   # a file shorter than its header says: the frames that are there come out, then the error
            ours = np.fromfile(o, dtype='<f4') if os.path.exists(o) and (run.returncode == 0 or trunc) else None
            if ref is None:
                print('%-40s FFmpeg cannot decode it; ours: %s' % (f, 'decoded %d values' % len(ours) if ours is not None else 'rejected')); continue
            rate, ch, r16 = ref
            if ours is None:
                print('%-40s FAIL: ours rejected the file (rc %d): %s' % (f, run.returncode, (run.stderr or run.stdout).strip().splitlines()[-1:] )); bad += 1; continue
            q = np.rint(ours.astype(np.float64) * 32768.0).astype(np.int64)
            n = min(len(q), len(r16))
            diff = int((q[:n] != r16[:n].astype(np.int64)).sum())
            ok = diff == 0 and len(q) == len(r16)           # a truncated file: FFmpeg also stops at the end of the data, so the lengths agree
            print('%-40s %s  %d ch %d Hz  %d samples (ffmpeg %d), differing: %d' % (f, ('OK  ' if ok else 'FAIL') + (' (file shorter than its header: frames then error)' if trunc else ''), ch, rate, len(q), len(r16), diff))
            if ok: n_ok += 1
            else: bad += 1
    finally:
        shutil.rmtree(w, ignore_errors=True)
    print('%d identical to FFmpeg, %d failures' % (n_ok, bad))
    return 1 if bad else 0

if __name__ == '__main__':
    sys.exit(main())
