#!/usr/bin/env python3
"""Decodes REAL files (a directory of MP4 / M4A files made by encoders that are not ours, e.g. the public xHE-AAC / HE-AAC / AAC-LC demo files of Fraunhofer IIS kept OUTSIDE the repository, untrusted data) with MaktabaSauti
(build/maktabasauti_cli) and compares the PCM with the reference decoders that are available: FFmpeg (ffmpeg binary; AAC-LC / HE-AAC), libxaac's xaacdec (Apache 2.0, built outside, default ~/Sauti-tools/libxaac/build; USAC) and the FDK
oracle (build/sauti_fdk_raw_oracle, black box; USAC, with a fitted gain: its default loudness normalisation differs).  The files are only opened by the decoders and tools/mp4_dump_aus.py (arguments are passed as lists, never through
a shell).  A window of WINDOW seconds after SKIP seconds is compared, the delay is searched (cross-correlation, +-4096 samples).  Output: one line per file.  No file = SKIP (exit 0).
usage: scripts/test_real_files.py DIR [WINDOW_SECONDS] [SKIP_SECONDS]   (TMPDIR for the scratch files: they can be large)"""
import os, re, shutil, struct, subprocess, sys, tempfile
import numpy as np
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
XAAC = os.path.join(os.environ.get('XAAC', os.path.expanduser('~/Sauti-tools/libxaac/build')), 'xaacdec')
FDK = os.path.join(ROOT, 'build', 'sauti_fdk_raw_oracle'); CLI = os.path.join(ROOT, 'build', 'maktabasauti_cli')
def snr(a, b, fit, ch):
    n = min(len(a), len(b), 400000 * ch)
    if n < 8000: return None
    x = a[:n:ch].astype(np.float64); y = b[:n:ch].astype(np.float64)                  # delay from the first channel
    m = 1 << int(np.ceil(np.log2(2 * n))); c = np.fft.irfft(np.fft.rfft(x, m) * np.conj(np.fft.rfft(y, m)), m); span = 4096
    lag = int(np.argmax(np.concatenate([c[-span:], c[:span + 1]]))) - span          # lag > 0: a is late
    sh = lag * ch
    aa = a[max(0, sh):]; bb = b[max(0, -sh):]; m2 = min(len(aa), len(bb)); aa = aa[:m2]; bb = bb[:m2]
    k = (np.abs(aa) < 0.99) & (np.abs(bb) < 0.99)
    if not k.any(): return None
    g = float((aa[k] * bb[k]).sum() / max((bb[k] ** 2).sum(), 1e-30)) if fit else 1.0
    e = float(((aa[k] - g * bb[k]) ** 2).sum()); s = float(((g * bb[k]) ** 2).sum())
    return (999.0 if e == 0 else 10 * np.log10(max(s, 1e-30) / e)), lag, g
def main():
    d = sys.argv[1]; win = float(sys.argv[2]) if len(sys.argv) > 2 else 8.0; skip = float(sys.argv[3]) if len(sys.argv) > 3 else 1.0
    files = sorted(f for f in os.listdir(d) if f.lower().endswith(('.mp4', '.m4a', '.aac', '.mp3')))
    if not files: print("SKIP: no files in", d); return 0
    for f in files:
        with tempfile.TemporaryDirectory(prefix='sauti-real.') as w:
            p = os.path.join(d, f); n = os.path.join(w, 'a'); o = subprocess.run([CLI, p, n + '.sau'], capture_output=True, text=True, timeout=1800)
            m = re.search(r'STREAM (\S+) in (\S+) rate=(\d+) channels=(\d+) total=(\d+) frames=(\d+)', o.stdout)
            if not m: print("%-64s ours: no stream (%s)" % (f, (o.stderr.strip() or o.stdout.strip())[:80])); continue
            codec, _, hz, ch, total, frames = m.group(1), m.group(2), int(m.group(3)), int(m.group(4)), int(m.group(5)), int(m.group(6))
            line = "%-64s %-9s %dch %5d Hz %8d fr%s" % (f, codec, ch, hz, frames, "" if o.returncode == 0 and frames >= total else "  DECODE ERROR: " + o.stderr.strip()[:40])
            a = np.fromfile(n + '.sau', '<f4'); lo = int(skip * hz) * ch; hi = min(len(a), lo + int(win * hz) * ch); ref = {}
            if shutil.which('ffmpeg') and not codec.endswith('USAC'):
                r = subprocess.run(['ffmpeg', '-v', 'error', '-i', p, '-f', 'f32le', '-acodec', 'pcm_f32le', n + '.ff'], capture_output=True)
                if r.returncode == 0 and os.path.exists(n + '.ff'): ref['FFmpeg'] = np.fromfile(n + '.ff', '<f4')
            if codec.endswith('USAC'):
                t = subprocess.run([sys.executable, '-I', os.path.join(ROOT, 'tools', 'mp4_dump_aus.py'), p, n], capture_output=True, text=True)
                if os.path.exists(n + '.aus'):
                    asc = open(n + '.asc', 'rb').read(); raw = open(n + '.aus', 'rb').read(); q = 0; aus = []
                    while q + 4 <= len(raw):
                        l = struct.unpack('>I', raw[q:q + 4])[0]; aus.append(raw[q + 4:q + 4 + l]); q += 4 + l
                    fl = round(total / max(len(aus), 1))
                    if os.path.exists(XAAC):
                        open(n + '.es', 'wb').write(asc + b''.join(aus))
                        tt = ['-dec_info_init:%d' % len(asc), '-g_track_count:1', '-ia_mp4_stsz_entries:%d' % len(aus), '-movie_time_scale:%d' % hz, '-media_time_scale:%d' % hz, '-playTimeInSamples:%d' % (len(aus) * fl), '-startOffsetInSamples:0', '-useEditlist:1']
                        open(n + '.txt', 'w').write('\n'.join(tt + ['-ia_mp4_stsz_size:%d' % len(x) for x in aus]) + '\n')
                        subprocess.run([XAAC, '-ifile:' + n + '.es', '-imeta:' + n + '.txt', '-mp4:1', '-ofile:' + n + '.pcm', '-pcmsz:16'], capture_output=True, timeout=1800)
                        if os.path.exists(n + '.pcm'): ref['libxaac'] = np.fromfile(n + '.pcm', '<i2').astype(np.float32) / 32768.0
                    if os.path.exists(FDK):
                        subprocess.run([FDK, n + '.asc', n + '.aus', n + '.fdk'], capture_output=True, timeout=1800)
                        if os.path.exists(n + '.fdk'): ref['FDK'] = np.fromfile(n + '.fdk', '<f4')
            for k, b in ref.items():
                if len(b) < hi - ch * 4096 or hi - lo < 8000: line += "  %s: too short" % k; continue
                am = np.trunc(a[lo:hi].astype(np.float64) * 32768.0) / 32768.0 if k == 'libxaac' else a[lo:hi]    # libxaac's 16-bit output TRUNCATES toward zero: quantise ours the same way, or quiet passages are limited by its quantisation noise
                r = snr(am, b[lo:hi + 8192 * ch], k == 'FDK', ch)
                line += "  %s %s" % (k, "%.1f dB" % r[0] if r else "n/a")
            if 'libxaac' in ref and 'FDK' in ref and len(ref['libxaac']) >= hi and len(ref['FDK']) >= hi + 8192 * ch:      # the two references against each other: which one is the outlier?
                r2 = snr(ref['libxaac'][lo:hi], ref['FDK'][lo:hi + 8192 * ch], True, ch)
                line += "  [libxaac vs FDK %s]" % ("%.1f dB" % r2[0] if r2 else "n/a")
            print(line); sys.stdout.flush()
    return 0
sys.exit(main())
