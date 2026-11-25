#!/usr/bin/env python3 -I
"""Buduje tests/data/alac/*.m4a (plik ALAC z kodera FFmpeg `alac` z syntetycznego sygnalu) i *.ref (int32 LE przeplecione: probki w skali natywnej, wartosc = f32 FFmpeg * 2^(glebia-1)).
Wzorzec: dekoder FFmpeg `alac` (niezalezny).  Uzycie: make_alac_fixtures.py   (wymaga ffmpeg i numpy; TMPDIR na pliki posrednie)."""
import numpy as np, subprocess, os, sys
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'alac'); TMP = os.environ.get('TMPDIR', '/tmp')
rng = np.random.default_rng(2611)
LAY = {1: 'mono', 2: 'stereo', 3: '3.0', 4: '4.0', 5: '5.0', 6: '5.1', 7: '6.1(back)', 8: '7.1(wide)'}
def signal(n, ch, kind, rate):
    t = np.arange(n) / rate; cols = []
    for c in range(ch):
        if kind == 'music': x = 0.4 * np.sin(2 * np.pi * 220 * (c + 1) * t) + 0.2 * np.sin(2 * np.pi * (1330 + 90 * c) * t) * np.exp(-3 * t % 1) + 0.02 * rng.standard_normal(n)
        elif kind == 'noise': x = 0.3 * rng.standard_normal(n)
        elif kind == 'silence_mix':
            x = 0.5 * np.sin(2 * np.pi * 440 * t); x[n // 4:n // 2] = 0; a, b = int(n * 0.7), int(n * 0.9); x[a:b] = 1e-5 * rng.standard_normal(b - a)
        elif kind == 'extreme': x = np.where(np.sin(2 * np.pi * 50 * t) > 0, 1.0, -1.0) * 0.999999; x[:3] = [1.0, -1.0, 0.0]
        elif kind == 'tiny': x = 2e-5 * rng.standard_normal(n)
        cols.append(np.clip(x, -1, 1))
    return np.stack(cols, 1)
FX = [  # nazwa, kanaly, glebia, sygnal, czestotliwosc, ramki, opcje kodera
    ('m16', 1, 16, 'music', 44100, 9000, []), ('s16_noise', 2, 16, 'noise', 44100, 5000, []), ('s24', 2, 24, 'music', 48000, 4300, []),
    ('c3', 3, 16, 'music', 44100, 1100, []), ('c5', 5, 16, 'music', 44100, 1100, []), ('c6', 6, 16, 'music', 48000, 1100, []), ('c7', 7, 16, 'music', 48000, 1100, []),
    ('c8', 8, 16, 'music', 48000, 1100, []), ('c6_24', 6, 24, 'music', 48000, 1100, []), ('silence', 2, 16, 'silence_mix', 44100, 9000, []),
    ('extreme', 2, 16, 'extreme', 44100, 4200, []), ('tiny24', 2, 24, 'tiny', 48000, 3000, []), ('verbatim', 2, 16, 'music', 44100, 4200, ['-compression_level', '0']),
    ('ord1', 2, 16, 'music', 44100, 4200, ['-min_prediction_order', '1', '-max_prediction_order', '1']),
    ('ord30', 2, 24, 'music', 44100, 4200, ['-min_prediction_order', '30', '-max_prediction_order', '30']), ('one', 2, 16, 'music', 44100, 1, []),
    ('r8k', 1, 16, 'music', 8000, 4100, []), ('s24_silence', 1, 24, 'silence_mix', 96000, 4300, []),
]
for name, ch, bits, kind, rate, n, opts in FX:
    x = signal(n, ch, kind, rate)
    if bits == 16: pcm = np.rint(x * 32767).astype('<i2'); fmt, sf = 's16le', 's16p'
    else: pcm = (np.rint(x * (2 ** (bits - 1) - 1)).astype('<i4') << (32 - bits)).astype('<i4'); fmt, sf = 's32le', 's32p'
    raw = os.path.join(TMP, name + '.raw'); pcm.tofile(raw); m4a = os.path.join(OUT, name + '.m4a')
    subprocess.run(['ffmpeg', '-v', 'error', '-y', '-f', fmt, '-ar', str(rate), '-channel_layout', LAY[ch], '-i', raw, '-c:a', 'alac', '-sample_fmt', sf] + opts + [m4a], check=True)
    f = np.frombuffer(subprocess.run(['ffmpeg', '-v', 'error', '-i', m4a, '-f', 'f32le', '-'], capture_output=True, check=True).stdout, '<f4')
    np.rint(f.astype('f8') * 2.0 ** (bits - 1)).astype('<i4').tofile(os.path.join(OUT, name + '.ref')); os.remove(raw)
print('ok', len(FX))
