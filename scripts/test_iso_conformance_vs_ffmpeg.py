#!/usr/bin/env python3
"""ISO/IEC 14496-26 conformance streams (downloaded by the user from standards.iso.org, NOT in the repository) decoded by build/maktabasauti_cli and by FFmpeg: the SNR at the best lag must reach
MIN_DB (default 100; float arithmetic on both sides) for the streams FFmpeg can decode.  The expected family comes from the file name: alNNsf_RR = AAC-LC with 960-line frames, apNN_48 = AAC LTP,
al960_sbr_* = HE-AAC / HE-AAC v2 with 960-line frames (FFmpeg has no SBR for 960: those files are compared with FDK by scripts/test_iso_conformance_vs_fdk.py), amNN = Main, alNN = LC.
usage: scripts/test_iso_conformance_vs_ffmpeg.py DIR [REGEX]   (DIR holds the .mp4 files; SKIP without ffmpeg / numpy / files)"""
import glob, os, re, subprocess, sys, tempfile
import numpy as np
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__))); CLI = os.path.join(ROOT, 'build', 'maktabasauti_cli')
MIN = float(os.environ.get('ISO_MIN_DB', '100'))
files = sorted(glob.glob(os.path.join(sys.argv[1], '*.mp4'))) if len(sys.argv) > 1 else []
if len(sys.argv) > 2: files = [f for f in files if re.search(sys.argv[2], os.path.basename(f))]
if not files or subprocess.run(['which', 'ffmpeg'], capture_output=True).returncode: print('SKIP: no files or no ffmpeg (this is NOT a proof)'); sys.exit(0)
W = tempfile.mkdtemp(prefix='sauti-iso.', dir=os.environ.get('TMPDIR') or None)
def best(a, b, maxlag):                                           # lag by FFT cross-correlation, then the SNR at the best lag (and its neighbours)
    n = min(len(a), len(b), 400000)
    if n < 2000: return None
    x = a[:n].astype(np.float64); y = b[:n].astype(np.float64); size = 1 << (2 * n - 1).bit_length()
    cc = np.fft.irfft(np.fft.rfft(x, size) * np.conj(np.fft.rfft(y, size)), size)
    lags = list(range(-maxlag, maxlag + 1)); vals = [cc[l % size] for l in lags]; centre = lags[int(np.argmax(vals))]; res = None
    for lag in (centre - 1, centre, centre + 1):
        xs = a[max(0, lag):][:300000].astype(np.float64); ys = b[max(0, -lag):][:len(xs)].astype(np.float64); m = min(len(xs), len(ys))
        if m < 2000: continue
        e = float(np.sum((xs[:m] - ys[:m]) ** 2)); sg = float(np.sum(ys[:m] ** 2)); snr = 999.0 if e == 0 else 10 * np.log10(sg / max(e, 1e-30))
        if res is None or snr > res[0]: res = (snr, lag)
    return res
def one(f):                                                       # -> (status, line): status OK / LOW / OURS / SKIP
    n = os.path.basename(f)[:-4]; o = os.path.join(W, n + '.f32'); t = os.path.join(W, n + '.ff')
    r = subprocess.run([CLI, f, o], capture_output=True, text=True); text = r.stdout + r.stderr
    if r.returncode or 'read:' in text or 'open:' in text: return 'OURS', '%-24s OURS FAIL %s' % (n, text.strip().splitlines()[-1:])
    ch = int(text.split('channels=')[1].split()[0])
    g = subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', f, '-f', 'f32le', '-acodec', 'pcm_f32le', t], capture_output=True, text=True)
    if g.returncode or not os.path.exists(t) or os.path.getsize(t) == 0: return 'SKIP', '%-24s FFmpeg cannot decode (%s)' % (n, g.stderr.strip().splitlines()[-1:] if g.stderr else '')
    a = np.fromfile(o, '<f4').reshape(-1, ch); b = np.fromfile(t, '<f4')
    if ch == 1 and len(b) % 2 == 0 and len(b) // 2 == len(a): b = b.reshape(-1, 2)[:, :1]           # FFmpeg "treats HE-AAC mono as stereo": the same signal in both channels
    if len(b) % ch: return 'SKIP', '%-24s FFmpeg channel count differs' % n
    b = b.reshape(-1, ch); res = [best(a[:, c], b[:, c], 2600) for c in range(ch)]; res = [x for x in res if x]
    if not res: return 'LOW', '%-24s too short to compare' % n
    snr = min(x[0] for x in res); lag = res[0][1]; flag = 'OK' if snr >= MIN else 'LOW'
    for fn in (o, t):
        try: os.remove(fn)
        except OSError: pass
    return flag, '%-24s %-4s %7.1f dB lag %5d  ch %d  lengths %d / %d' % (n, flag, snr, lag, ch, len(a), len(b))
if __name__ == '__main__':
    from multiprocessing import Pool
    with Pool(int(os.environ.get('ISO_JOBS', '8'))) as pool: results = pool.map(one, files, chunksize=2)
    for st, line in results: print(line)
    count = {k: sum(1 for st, _ in results if st == k) for k in ('OK', 'LOW', 'OURS', 'SKIP')}
    print('%d files: %d ok, %d below %.0f dB, %d our decoder fails, %d FFmpeg cannot decode' % (len(files), count['OK'], count['LOW'], MIN, count['OURS'], count['SKIP']))
    sys.exit(1 if count['LOW'] or count['OURS'] else 0)
