#!/usr/bin/env python3
"""G.711.1 against the ITU-T reference simulation software (a BLACK-BOX oracle built OUTSIDE the repository from the electronic attachment of Rec. ITU-T G.711.1,
nothing of it is linked or copied): for several signals, both laws and modes the reference encoder makes the stream, the reference decoder and tools/maktabasauti_cli (forced codec
g7111-raw) decode it, and the samples are compared.
usage: scripts/test_g7111_vs_itu.py [ITU_DIR] [AUDIO_FILE]
  ITU_DIR  = the directory with the extracted G.711.1_MB-Fix_v2.00 (default ~/Sauti-tools/itu-g7111/x/G.711.1_MB-Fix_v2.00; its test_codec encoder / decoder must be built)
SKIP (stated loudly, exit 0) without the reference binaries or numpy: that is not a proof of agreement."""
import os, subprocess, sys, tempfile
try:
    import numpy as np
except Exception as e:
    print('SKIP: no numpy (%s)' % e); sys.exit(0)
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
itu = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser('~/Sauti-tools/itu-g7111/x/G.711.1_MB-Fix_v2.00')
enc = os.path.join(itu, 'src', 'test_codec', 'encoder'); dec = os.path.join(itu, 'src', 'test_codec', 'decoder')
if not (os.access(enc, os.X_OK) and os.access(dec, os.X_OK)):
    print('SKIP: the ITU reference encoder / decoder are not built in %s' % itu); sys.exit(0)
subprocess.run(['make', '-s', 'build/maktabasauti_cli'], cwd=root, check=True, capture_output=True)
cli = os.path.join(root, 'build', 'maktabasauti_cli')
rng = np.random.default_rng(711)
def signals(wb):
    fs = 16000 if wb else 8000; n = fs * 4; t = np.arange(n) / fs; out = {}
    music = 0.3 * sum(np.sin(2 * np.pi * (180 + 30 * np.sin(2 * np.pi * 0.3 * t)) * k * t) / k for k in range(1, 20) if 180 * k < fs / 2.2) + 0.2 * np.sin(2 * np.pi * (200 + (fs / 8) * t / 4) * t)
    bursts = (np.sin(2 * np.pi * 2.0 * t) > 0.8) * rng.standard_normal(n) * 0.2
    out['music'] = music + bursts
    out['quiet'] = (music + bursts) * 0.004                                                            # ~ -48 dB: the low-level paths (attenuated shaping filter, dead zone, noise gate)
    out['noise'] = rng.standard_normal(n) * 0.25
    out['loud'] = np.clip(1.6 * music + bursts, -1, 1)                                                 # clipping
    sil = music.copy(); sil[n // 4: n // 2] = 0.0; sil[3 * n // 4:] *= 0.001; out['silence'] = sil     # digital silence and fade
    if len(sys.argv) > 2:
        subprocess.run(['ffmpeg', '-v', 'error', '-y', '-ss', '30', '-t', '5', '-i', sys.argv[2], '-ar', str(fs), '-ac', '1', '-f', 's16le', 'pipe:1'], check=True, stdout=subprocess.PIPE).stdout
        out['file'] = np.frombuffer(subprocess.run(['ffmpeg', '-v', 'error', '-y', '-ss', '30', '-t', '5', '-i', sys.argv[2], '-ar', str(fs), '-ac', '1', '-f', 's16le', 'pipe:1'], check=True, stdout=subprocess.PIPE).stdout, dtype='<i2') / 32768.0
    return out
modes = [int(m) for m in os.environ.get('G7111_MODES', '1,2,3,4').split(',')]
fail = 0
with tempfile.TemporaryDirectory(prefix='sauti-g7111.') as w:
    for mode in modes:
        wb = mode in (3, 4)
        for name, x in signals(wb).items():
            pcm = np.clip(x * 32767, -32768, 32767).astype('<i2'); pcm.tofile(w + '/in.raw')
            for law in ('A', 'u'):
                args = [enc, '-quiet', '-mode', str(mode), '-hardbit'] + ([] if wb else ['-nb']) + [law, w + '/in.raw', w + '/s.hard']
                subprocess.run(args, check=True, capture_output=True)
                subprocess.run([dec, '-quiet', '-hardbit', law, '-mode', str(mode), w + '/s.hard', w + '/ref.raw'], check=True, capture_output=True)
                r = subprocess.run([cli, w + '/s.hard', w + '/our.f32', '--codec', 'g7111-raw', '--opt', 'g7111.mode=%d' % mode, '--opt', 'g7111.law=' + law.lower()], capture_output=True)
                if r.returncode != 0:
                    print('FAIL mode %d %-7s %s: %s' % (mode, name, law, r.stderr.decode().strip())); fail = 1; continue
                ref = np.fromfile(w + '/ref.raw', dtype='<i2').astype(np.int64); mine = np.round(np.fromfile(w + '/our.f32', dtype=np.float32) * 32768).astype(np.int64)
                n = min(len(ref), len(mine)); d = np.abs(ref[:n] - mine[:n]); same = int((d == 0).sum())
                sig = float(np.sum(ref[:n].astype(float) ** 2)); err = float(np.sum((ref[:n] - mine[:n]).astype(float) ** 2))
                snr = 999.0 if err == 0 else 10 * np.log10(max(sig, 1e-9) / err)
                # the lower band (R1, R2a) is bit exact; the higher band (R2b, R3) is an equivalent implementation: the reference computes its MDCT in 16 bit fixed point with Q15 FFT stages
                # (a noise floor of about -57 dB in the 4 - 8 kHz band) and ours in double precision, so the requirement there is an SNR
                ok = len(ref) == len(mine) and ((int(d.max()) == 0) if not wb else (snr >= float(os.environ.get('G7111_WB_SNR', '55')) or int(d.max()) <= 2))
                # the same stream in the ITU-T G.192 (soft bit) format, recognised by the registry without any hint, must decode to exactly the same samples
                subprocess.run([enc, '-quiet', '-mode', str(mode)] + ([] if wb else ['-nb']) + [law, w + '/in.raw', w + '/s.g192'], check=True, capture_output=True)
                rg = subprocess.run([cli, w + '/s.g192', w + '/g192.f32', '--opt', 'g7111.mode=%d' % mode, '--opt', 'g7111.law=' + law.lower()], capture_output=True)
                same_g192 = rg.returncode == 0 and np.array_equal(np.fromfile(w + '/g192.f32', dtype=np.float32), np.fromfile(w + '/our.f32', dtype=np.float32))
                if not same_g192: print('FAIL mode %d %-7s %s: the G.192 stream decodes differently from the hard bit stream (%s)' % (mode, name, law, rg.stderr.decode().strip())); ok = False
                print('%s mode %d %-7s %s: %d / %d samples identical, max diff %d, SNR %.1f dB' % ('OK  ' if ok else 'FAIL', mode, name, law, same, n, int(d.max()), snr))
                fail |= not ok
sys.exit(1 if fail else 0)
