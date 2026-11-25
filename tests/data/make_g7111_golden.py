#!/usr/bin/env python3
"""Regenerates tests/g7111_golden.h: streams made by the ITU-T G.711.1 reference ENCODER and the samples of the reference DECODER (the reference simulation software is a black-box oracle built
OUTSIDE the repository from the electronic attachment of Rec. ITU-T G.711.1; nothing of it is copied).  36 frames of a synthetic signal: music, a very quiet passage, noise, a clipped passage,
digital silence and a fade, so the low-level paths (attenuated shaping filter, dead zone, noise gate) are exercised.  R1 / R2a: the golden is the FNV-1a 64 of the reference samples (the decoder is
bit exact there); R2b / R3 keep the reference samples (the decoder is an equivalent implementation, checked as SNR).
usage: tests/data/make_g7111_golden.py [ITU_DIR] > tests/g7111_golden.h"""
import numpy as np, os, subprocess, sys, tempfile
itu = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser('~/Sauti-tools/itu-g7111/x/G.711.1_MB-Fix_v2.00')
enc = os.path.join(itu, 'src', 'test_codec', 'encoder'); dec = os.path.join(itu, 'src', 'test_codec', 'decoder')
FR = 36
def tonal(fs):                      # a resonant tone with a little noise: the perceptual filter is very selective (found by searching for a signal that separates 0.9999 from 1 / 1.0001 in the lag window)
    n = FR * fs // 200; t = np.arange(n) / fs; rng = np.random.default_rng(52); f0 = 150 + rng.uniform(0, 600)
    x = rng.uniform(0.1, 0.6) * np.sin(2 * np.pi * f0 * t) + rng.uniform(0, 0.3) * np.sin(2 * np.pi * (f0 * 2.7) * t) + rng.uniform(0, 0.1) * rng.standard_normal(n)
    return np.clip(x * 32767, -32768, 32767).astype('<i2')
def signal(fs):
    n = FR * fs // 200; t = np.arange(n) / fs; rng = np.random.default_rng(7111)
    music = 0.3 * sum(np.sin(2 * np.pi * (180 + 30 * np.sin(2 * np.pi * 1.5 * t)) * k * t) / k for k in range(1, 20) if 180 * k < fs / 2.2) + 0.15 * np.sin(2 * np.pi * (300 + fs / 4.0 * t / (n / fs)) * t)
    x = music.copy(); seg = n // 6
    x[seg:2 * seg] = music[seg:2 * seg] * 0.004                       # very quiet
    x[2 * seg:3 * seg] = rng.standard_normal(seg) * 0.2               # noise
    x[3 * seg:4 * seg] = np.clip(1.8 * music[3 * seg:4 * seg], -1, 1) # clipped
    x[4 * seg:5 * seg] = 0.0                                          # digital silence
    x[5 * seg:] = music[5 * seg:] * np.linspace(1, 0.001, n - 5 * seg)  # fade
    return np.clip(x * 32767, -32768, 32767).astype('<i2')
def fnv(a):
    h = 0xcbf29ce484222325
    for v in a.astype('<u2').tolist():
        for b in (v & 255, v >> 8): h = ((h ^ b) * 0x100000001b3) & 0xffffffffffffffff
    return h
cases = [('R1', 1, 'A', 'mix'), ('R1', 1, 'u', 'mix'), ('R2a', 2, 'A', 'mix'), ('R2a', 2, 'u', 'mix'), ('R2b', 3, 'A', 'mix'), ('R3', 4, 'A', 'mix'), ('R3', 4, 'u', 'mix'), ('R2a', 2, 'A', 'tonal')]
out = ['/* ITU-T G.711.1 golden vectors (tests/data/make_g7111_golden.py): 36 frames of a synthetic signal encoded and decoded by the ITU-T reference software. */\n',
       'typedef struct { int mode, alaw; const uint8_t *bits; size_t bits_n; size_t samples; uint64_t fnv; const int16_t *ref; } G7111Golden;\n']
rows = []
with tempfile.TemporaryDirectory(prefix='sauti-g7111gold.') as w:
    for name, mode, law, kind in cases:
        wb = mode in (3, 4); x = (tonal if kind == 'tonal' else signal)(16000 if wb else 8000); x.tofile(w + '/in.raw')
        subprocess.run([enc, '-quiet', '-mode', str(mode), '-hardbit'] + ([] if wb else ['-nb']) + [law, w + '/in.raw', w + '/s.hard'], check=True, capture_output=True)
        subprocess.run([dec, '-quiet', '-hardbit', law, '-mode', str(mode), w + '/s.hard', w + '/ref.raw'], check=True, capture_output=True)
        bits = open(w + '/s.hard', 'rb').read(); ref = np.fromfile(w + '/ref.raw', dtype='<i2')
        tag = '%s_%s%s' % (name.lower(), law.lower(), '_tonal' if kind == 'tonal' else '')
        out.append('static const uint8_t g7111_bits_%s[] = {%s};\n' % (tag, ','.join(str(b) for b in bits)))
        if wb: out.append('static const int16_t g7111_ref_%s[] = {%s};\n' % (tag, ','.join(str(int(v)) for v in ref)))
        rows.append((mode, 1 if law == 'A' else 0, tag, len(bits), len(ref), fnv(ref), wb))
out.append('static const G7111Golden g7111_golden[] = {\n')
for mode, alaw, tag, nb, ns, h, wb in rows:
    out.append('    { %d, %d, g7111_bits_%s, %d, %d, UINT64_C(0x%016x), %s },\n' % (mode, alaw, tag, nb, ns, h, 'g7111_ref_' + tag if wb else 'NULL'))
out.append('};\n#define G7111_GOLDEN_COUNT %d\n#define G7111_GOLDEN_FRAMES %d\n' % (len(rows), FR))
sys.stdout.write(''.join(out))
