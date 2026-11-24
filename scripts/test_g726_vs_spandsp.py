#!/usr/bin/env python3
"""G.726 against an INDEPENDENT implementation: libspandsp (black-box oracle through ctypes, nothing linked).  Encodes a signal (default: 8 s of a given audio file, or a synthetic
signal) with spandsp at 16 / 24 / 32 / 40 kbit/s in both bit orders, decodes the same bytes with spandsp and with MaktabaSauti (tools/maktabasauti_cli, forced codec g726-raw), and
requires the samples to be IDENTICAL.  FFmpeg's g726 decoder is not a valid oracle (it differs from the ITU algorithm by up to 14640 on the same codes).
usage: scripts/test_g726_vs_spandsp.py [AUDIO_FILE]   (needs libspandsp.so.2, numpy; SKIP, stated loudly, exit 0 without them: that is not a proof of agreement)"""
import ctypes, os, subprocess, sys, tempfile
try:
    import numpy as np
    lib = ctypes.CDLL('libspandsp.so.2')
except Exception as e:
    print('SKIP: no libspandsp / numpy (%s)' % e); sys.exit(0)
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
subprocess.run(['make', '-s', 'build/maktabasauti_cli'], cwd=root, check=True, capture_output=True)
cli = os.path.join(root, 'build', 'maktabasauti_cli')
lib.g726_init.restype = ctypes.c_void_p; lib.g726_init.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int]
lib.g726_encode.restype = ctypes.c_int; lib.g726_encode.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int]
lib.g726_decode.restype = ctypes.c_int; lib.g726_decode.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int]
with tempfile.TemporaryDirectory(prefix='sauti-g726.') as w:
    if len(sys.argv) > 1:
        subprocess.run(['ffmpeg', '-v', 'error', '-y', '-ss', '30', '-t', '8', '-i', sys.argv[1], '-ar', '8000', '-ac', '1', '-f', 's16le', w + '/in.raw'], check=True)
        pcm = np.fromfile(w + '/in.raw', dtype='<i2')
    else:
        t = np.arange(64000) / 8000.0; rng = np.random.default_rng(5)
        pcm = (0.25 * sum(np.sin(2 * np.pi * 150 * k * t) / k for k in range(1, 12)) + 0.05 * rng.standard_normal(len(t))) * 32767
        pcm = np.clip(pcm, -32768, 32767).astype('<i2')
    fail = 0
    for bits in (2, 3, 4, 5):
        for name, pk in (('be', 1), ('le', 2)):
            s = lib.g726_init(None, bits * 8000, 0, pk); out = ctypes.create_string_buffer(len(pcm) * 2)
            n = lib.g726_encode(s, out, pcm.ctypes.data, len(pcm)); data = out.raw[:n]
            open(w + '/x.g726', 'wb').write(data)
            s = lib.g726_init(None, bits * 8000, 0, pk); ref = np.zeros(len(pcm) + 16, dtype='<i2'); buf = ctypes.create_string_buffer(data, len(data))
            m = lib.g726_decode(s, ref.ctypes.data, buf, len(data)); ref = ref[:m].astype(np.int64)
            subprocess.run([cli, w + '/x.g726', w + '/y.f32', '--codec', 'g726-raw', '--opt', 'raw.bits=%d' % bits, '--opt', 'raw.order=' + name], check=True, capture_output=True)
            mine = np.round(np.fromfile(w + '/y.f32', dtype=np.float32) * 32768).astype(np.int64)
            ok = len(mine) == len(ref) and np.array_equal(mine, ref)
            print('%s %d kbit/s %s: %d samples, %s' % ('OK  ' if ok else 'FAIL', bits * 8, name, len(ref), 'identical' if ok else 'max diff %d' % (np.abs(mine[:min(len(mine), len(ref))] - ref[:min(len(mine), len(ref))]).max() if len(ref) else -1)))
            fail |= not ok
    sys.exit(1 if fail else 0)
