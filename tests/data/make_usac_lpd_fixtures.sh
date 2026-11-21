#!/usr/bin/env bash
# Regenerates tests/data/usac_l1..l3.m4a: REAL USAC streams with the LPD core (ACELP / TCX), made by an independent encoder, libxaac (https://github.com/ittiam-systems/libxaac, Apache License 2.0, built OUTSIDE this
# repository, default ~/Sauti-tools/libxaac/build/xaacenc; its encoder codes LPD only at 16 and 24 kHz) from SYNTHETIC signals generated below (a glottal pulse train through three formant resonators with breath
# noise = speech-like; a harmonic chord with noise bursts = music-like; speech then noise).  `-usac:2` = "td" mode: every frame is LPD.  Only the first 16 access units are kept (the first carries two pre-roll units).
#   usac_l1  mono 16 kHz speech-like (ACELP)     usac_l2  mono 24 kHz music-like (TCX 1024 and mixed)     usac_l3  mono 16 kHz speech then noise (ACELP / TCX switching)
# usage: tests/data/make_usac_lpd_fixtures.sh   (needs python3 + numpy and the libxaac encoder; XAAC=<dir with xaacenc> to override)
set -euo pipefail
cd "$(dirname "$0")"
ENC="${XAAC:-$HOME/Sauti-tools/libxaac/build}/xaacenc"
[ -x "$ENC" ] || { echo "no libxaac encoder at $ENC"; exit 1; }
W="$(mktemp -d "${TMPDIR:-/tmp}/sauti-lpd.XXXXXX")"; trap 'rm -rf -- "${W:?}"' EXIT
python3 -I - "$W" <<'PY'
import numpy as np, struct, sys
W = sys.argv[1]; rng = np.random.default_rng(7)
def wav(name, x, fs):
    d = (np.clip(x, -1, 1) * 32767).astype('<i2').tobytes()
    open("%s/%s.wav" % (W, name), 'wb').write(b'RIFF' + struct.pack('<I', 36 + len(d)) + b'WAVEfmt ' + struct.pack('<IHHIIHH', 16, 1, 1, fs, fs * 2, 2, 16) + b'data' + struct.pack('<I', len(d)) + d)
def res(x, f, bw, fs):
    r = np.exp(-np.pi * bw / fs); th = 2 * np.pi * f / fs; a1 = -2 * r * np.cos(th); a2 = r * r; y = np.zeros_like(x)
    for i in range(2, len(x)): y[i] = x[i] - a1 * y[i - 1] - a2 * y[i - 2]
    return y
def speech(fs, n, t):
    f0 = 110 + 50 * np.sin(2 * np.pi * 0.7 * t); ph = np.cumsum(f0) / fs; v = (np.diff(np.floor(ph), prepend=0) > 0).astype(float)
    for f, bw in ((700, 90), (1220, 110), (2600, 160)): v = res(v, f, bw, fs)
    return v / np.max(np.abs(v)) * 0.4 * (0.6 + 0.4 * np.sin(2 * np.pi * 2.5 * t)) + 0.01 * rng.standard_normal(n)
fs = 16000; n = fs * 2; t = np.arange(n) / fs; sp16 = speech(fs, n, t); wav('l1', sp16, fs)
fs = 24000; n = fs * 2; t = np.arange(n) / fs
m = sum(0.08 / h * np.sin(2 * np.pi * f * h * t) for f in (196, 247, 294) for h in range(1, 12)) + 0.15 * rng.standard_normal(n) * (np.sin(2 * np.pi * 1.5 * t) > 0.7)
wav('l2', np.clip(m, -0.9, 0.9), fs)
wav('l3', np.concatenate([sp16[:8000], 0.2 * rng.standard_normal(8000)]), 16000)
PY
for n in l1 l2 l3; do
  "$ENC" -ifile:"$W/$n.wav" -ofile:"$W/$n.es" -aot:42 -usac:2 -esbr:0 -br:24000 >/dev/null
  python3 -I - "$W" "$n" <<'PY'
import re, struct, sys
W, n = sys.argv[1], sys.argv[2]
es = open("%s/%s.es" % (W, n), 'rb').read(); txt = open("%s/%s.txt" % (W, n)).read()
cfg = int(re.search(r'dec_info_init:(\d+)', txt).group(1)); sizes = [int(x) for x in re.findall(r'ia_mp4_stsz_size:(\d+)', txt)][:16]
open("%s/%s.asc" % (W, n), 'wb').write(es[:cfg]); p = cfg; out = b''
for s in sizes: out += struct.pack('>I', s) + es[p:p + s]; p += s
open("%s/%s.aus" % (W, n), 'wb').write(out)
PY
  python3 -I ../../tools/mux_raw_aus_mp4.py "$W/$n" "usac_$n.m4a" 1024
done
