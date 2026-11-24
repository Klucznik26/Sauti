#!/usr/bin/env python3
"""Regenerates tests/g726_golden.h: ITU-T G.726 test vectors made with an INDEPENDENT implementation, libspandsp (libspandsp.so.2, used as a black-box oracle through ctypes;
nothing of it is linked or copied).  A synthetic 8 kHz signal (3000 samples: harmonics, a sweep, a noise burst, silence) is encoded at 16 / 24 / 32 / 40 kbit/s with both bit orders
(left-justified = big-endian, right-justified = little-endian), and the same library decodes it again: the decoded 16 bit samples are the golden values (FNV-1a 64 of all samples
plus the first 8).  FFmpeg's g726 decoder is NOT used as the oracle: it differs from the ITU algorithm by up to 14640 on the same codes (measured 2026-11-25).
usage: tests/data/make_g726_golden.py > tests/g726_golden.h"""
import ctypes, numpy as np, sys
lib = ctypes.CDLL('libspandsp.so.2')
lib.g726_init.restype = ctypes.c_void_p; lib.g726_init.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int]
lib.g726_encode.restype = ctypes.c_int; lib.g726_encode.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int]
lib.g726_decode.restype = ctypes.c_int; lib.g726_decode.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int]
N = 3000
t = np.arange(N) / 8000.0
x = 0.30 * sum(np.sin(2 * np.pi * (140 * k) * t) / k for k in range(1, 12))
x += 0.15 * np.sin(2 * np.pi * (300 + 2500 * t / (N / 8000.0)) * t)
rng = np.random.default_rng(726)
x[1200:1500] += 0.25 * rng.standard_normal(300)
x[2400:2700] = 0.0
pcm = np.clip(x / max(1e-9, np.abs(x).max()) * 0.6 * 32767, -32768, 32767).astype('<i2')
def fnv(a):
    h = 0xcbf29ce484222325
    for v in a.astype('<u2').tolist():
        for byte in (v & 255, v >> 8): h = ((h ^ byte) * 0x100000001b3) & 0xffffffffffffffff
    return h
print('/* ITU-T G.726 golden vectors (tests/data/make_g726_golden.py): 3000 samples of a synthetic signal encoded and decoded by libspandsp. */')
print('typedef struct { unsigned bits; const uint8_t *be; size_t be_n; const uint8_t *le; size_t le_n; uint64_t fnv; int16_t first[8]; } G726Golden;')
rows = []
for bits in (2, 3, 4, 5):
    br = bits * 8000
    enc = {}
    for name, pk in (('be', 1), ('le', 2)):
        s = lib.g726_init(None, br, 0, pk); out = ctypes.create_string_buffer(N * 2)
        n = lib.g726_encode(s, out, pcm.ctypes.data, N); enc[name] = out.raw[:n]
    s = lib.g726_init(None, br, 0, 1); dec = np.zeros(N + 16, dtype='<i2'); buf = ctypes.create_string_buffer(enc['be'], len(enc['be']))
    n = lib.g726_decode(s, dec.ctypes.data, buf, len(enc['be'])); dec = dec[:n]
    s = lib.g726_init(None, br, 0, 2); dec2 = np.zeros(N + 16, dtype='<i2'); buf = ctypes.create_string_buffer(enc['le'], len(enc['le']))
    n2 = lib.g726_decode(s, dec2.ctypes.data, buf, len(enc['le'])); assert n == n2 == N and (dec == dec2[:n2]).all()
    for name in ('be', 'le'):
        print('static const uint8_t g726_%s_%d[] = {%s};' % (name, bits, ','.join(str(b) for b in enc[name])))
    rows.append((bits, len(enc['be']), len(enc['le']), fnv(dec), dec[:8].tolist()))
print('static const G726Golden g726_golden[4] = {')
for bits, nb, nl, h, first in rows:
    print('    { %d, g726_be_%d, %d, g726_le_%d, %d, UINT64_C(0x%016x), {%s} },' % (bits, bits, nb, bits, nl, h, ','.join(str(v) for v in first)))
print('};')
print('#define G726_GOLDEN_SAMPLES %d' % N)
