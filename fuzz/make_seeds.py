#!/usr/bin/env python3
"""Generuje korpus poczatkowy dla celow fuzzingu z plikow tests/data (syntetycznych, bez danych uzytkownika): fuzz/make_seeds.py OUT_DIR.
Cele open/container maja bajty sterujace na poczatku (patrz fuzz_open.c / fuzz_container.c), wiec ziarna dostaja prefiksy; probe dostaje surowe pliki; hostile_io skrypty."""
import os, sys, glob, random
out = sys.argv[1]
files = sorted(glob.glob('tests/data/*.aac') + glob.glob('tests/data/*.loas') + glob.glob('tests/data/*.adif') + glob.glob('tests/data/*.m4a') + glob.glob('tests/data/probe_real.*'))
for t in ('probe', 'open', 'container', 'hostile_io'):
    os.makedirs(os.path.join(out, t), exist_ok=True)
def put(t, name, data): open(os.path.join(out, t, name), 'wb').write(data)
codec_of = lambda f: 1 if f.endswith('.loas') else 2 if f.endswith('.adif') else 3 if f.endswith('.m4a') else 0
for f in files:
    d = open(f, 'rb').read(); base = os.path.basename(f)
    put('probe', base, d)
    for mode in (0b00001, 0b00011, 0b00000, 0b01001, 0b10001):                   # przewijalne/nie, rozmiar, porcje, blokowanie
        put('open', '%s.%02x' % (base, mode), bytes([mode, 7]) + d)
    for seek in (0, 4):
        put('container', '%s.%d' % (base, seek), bytes([codec_of(f) | seek]) + d)
    put('probe', base + '.cut', d[:len(d) // 2])
# pary sklejone (zmiana parametrow w srodku strumienia ADTS/LOAS)
pairs = [('lc.aac', 'lc_mono.aac'), ('lc.aac', 'lc44.aac'), ('he2.aac', 'lc.aac'), ('lc.aac', 'he1.aac'), ('lc.loas', 'he1.loas'), ('he2.loas', 'lc44.loas')]
for a, b in pairs:
    pa, pb = os.path.join('tests/data', a), os.path.join('tests/data', b)
    if os.path.exists(pa) and os.path.exists(pb):
        d = open(pa, 'rb').read() + open(pb, 'rb').read(); n = a + '+' + b
        put('open', n + '.01', bytes([0b00001, 7]) + d); put('open', n + '.00', bytes([0b00000, 7]) + d)
        put('container', n, bytes([codec_of(a)]) + d)
rng = random.Random(7)
for i in range(16):
    put('hostile_io', 'script%02d' % i, bytes(rng.randrange(256) for _ in range(8 + 64)))
# syntetyczne pliki wszystkich formatow z tests/mks_probe_corpus.h (narzedzie fuzz/dump_corpus.c, budowane przez make): argv[2] = katalog z zrzutem
if len(sys.argv) > 2:
    for f in sorted(glob.glob(os.path.join(sys.argv[2], 'syn_*'))):
        d = open(f, 'rb').read(); base = os.path.basename(f)
        put('probe', base, d)
        for mode in (0b00001, 0b00000):
            put('open', '%s.%02x' % (base, mode), bytes([mode, 7]) + d)
        for codec in range(4):
            put('container', '%s.c%d' % (base, codec), bytes([codec]) + d)
print('seeds in', out, sum(len(os.listdir(os.path.join(out, t))) for t in ('probe', 'open', 'container', 'hostile_io')))
