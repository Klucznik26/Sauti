#!/usr/bin/env python3 -I
"""Buduje tests/data/tta/*.tta (koder FFmpeg `tta` z syntetycznych sygnalow) i *.ref (probki w skali natywnej: int16 LE dla 8 i 16 bit, int32 LE dla 24 bit; wartosc = f32 FFmpeg * 2^(8 * bajty - 1)).
Wzorzec: dekoder FFmpeg `tta` (niezalezny).  Uzycie: make_tta_fixtures.py   (wymaga ffmpeg i numpy; TMPDIR na pliki posrednie)."""
import numpy as np, subprocess, os
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'tta'); TMP = os.environ.get('TMPDIR', '/tmp')
rng = np.random.default_rng(1128)
LAY = {1: 'mono', 2: 'stereo', 3: '3.0', 4: '4.0', 5: '5.0', 6: '5.1'}
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
FX = [  # nazwa, kanaly, glebia, sygnal, czestotliwosc, probki (ramka = 256 * czestotliwosc / 245: 8359 przy 8 kHz, 46080 przy 44,1 kHz)
    ('m16', 1, 16, 'music', 8000, 17000), ('s16', 2, 16, 'music', 8000, 17000), ('s8', 2, 8, 'music', 8000, 8500), ('s24', 2, 24, 'music', 8000, 8500),
    ('c3', 3, 16, 'music', 8000, 3000), ('c6', 6, 16, 'music', 8000, 3000), ('silence', 2, 16, 'silence_mix', 8000, 17000), ('extreme', 2, 16, 'extreme', 8000, 4000),
    ('noise24', 1, 24, 'noise', 8000, 4000), ('tiny', 2, 16, 'tiny', 8000, 8500), ('one', 2, 16, 'music', 8000, 1), ('exact', 2, 16, 'music', 8000, 8359),
    ('plus1', 2, 16, 'music', 8000, 8360), ('m8_noise', 1, 8, 'noise', 8000, 4000), ('r44k', 1, 16, 'music', 44100, 46081),
]
for name, ch, bits, kind, rate, n in FX:
    x = signal(n, ch, kind, rate)
    if bits == 8: pcm = (np.rint(x * 127) + 128).astype('u1'); fmt, sf = 'u8', 'u8'
    elif bits == 16: pcm = np.rint(x * 32767).astype('<i2'); fmt, sf = 's16le', 's16'
    else: pcm = (np.rint(x * (2 ** 23 - 1)).astype('<i4') << 8).astype('<i4'); fmt, sf = 's32le', 's32'
    raw = os.path.join(TMP, name + '.raw'); pcm.tofile(raw); tta = os.path.join(OUT, name + '.tta')
    subprocess.run(['ffmpeg', '-v', 'error', '-y', '-f', fmt, '-ar', str(rate), '-channel_layout', LAY[ch], '-i', raw, '-c:a', 'tta', '-sample_fmt', sf, tta], check=True)
    f = np.frombuffer(subprocess.run(['ffmpeg', '-v', 'error', '-i', tta, '-f', 'f32le', '-'], capture_output=True, check=True).stdout, '<f4')
    bytes_ = (bits + 7) // 8
    v = np.rint(f.astype('f8') * 2.0 ** (8 * bytes_ - 1))
    (v.astype('<i2') if bytes_ <= 2 else v.astype('<i4')).tofile(os.path.join(OUT, name + '.ref')); os.remove(raw)
print('ok', len(FX))
