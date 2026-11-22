#!/usr/bin/env bash
# Regenerates tests/data/usac_p1..p3.m4a: eSBR 2:1 with predictive vector coding (PVC, mode 2) from SYNTHETIC signals, made by the encoder of libxaac (Apache License 2.0, built OUTSIDE this repository, default
# ~/Sauti-tools/libxaac/build/xaacenc).  libxaac produces PVC frames only for a MONO signal coded in the time domain: -usac:2 (LPD core), -ccfl_idx:3, 24 kHz core (output 48 kHz), -esbr:1 -pvc_enc:1.  Its
# encoder never uses a variable length grid or a sine inside a PVC frame; those syntax elements are covered by the generator streams of make_usac_pvc_gen_fixtures.sh.
# usage: tests/data/make_usac_pvc_fixtures.sh   (needs python3 + numpy and the libxaac encoder; XAAC=<dir with xaacenc> to override)
set -euo pipefail
cd "$(dirname "$0")/../.."
ENC="${XAAC:-$HOME/Sauti-tools/libxaac/build}/xaacenc"
[ -x "$ENC" ] || { echo "no libxaac encoder at $ENC"; exit 1; }
W="$(mktemp -d "${TMPDIR:-/tmp}/sauti-pvc.XXXXXX")"; trap 'rm -rf -- "${W:?}"' EXIT
python3 -I - "$W" <<'PY'
import numpy as np, struct, sys
W = sys.argv[1]; rng = np.random.default_rng(7)
def wav(name, x, fs, ch=1):
    d = (np.clip(x, -1, 1) * 32767).astype('<i2').tobytes()
    open("%s/%s.wav" % (W, name), 'wb').write(b'RIFF' + struct.pack('<I', 36 + len(d)) + b'WAVEfmt ' + struct.pack('<IHHIIHH', 16, 1, ch, fs, fs * 2 * ch, 2 * ch, 16) + b'data' + struct.pack('<I', len(d)) + d)
def res(x, f, bw, fs):
    r = np.exp(-np.pi * bw / fs); th = 2 * np.pi * f / fs; a1 = -2 * r * np.cos(th); a2 = r * r; y = np.zeros_like(x)
    for i in range(2, len(x)): y[i] = x[i] - a1 * y[i - 1] - a2 * y[i - 2]
    return y
def speech(fs, n, t):
    f0 = 110 + 50 * np.sin(2 * np.pi * 0.7 * t); ph = np.cumsum(f0) / fs; v = (np.diff(np.floor(ph), prepend=0) > 0).astype(float)
    for f, bw in ((700, 90), (1220, 110), (2600, 160)): v = res(v, f, bw, fs)
    return v / np.max(np.abs(v)) * 0.4 * (0.6 + 0.4 * np.sin(2 * np.pi * 2.5 * t)) + 0.01 * rng.standard_normal(n)
fs = 24000; n = fs * 2; t = np.arange(n) / fs; sp24 = speech(fs, n, t); wav('p1', sp24, fs)
m = sum(0.08 / h * np.sin(2 * np.pi * f * h * t) for f in (196, 247, 294) for h in range(1, 12)) + 0.15 * rng.standard_normal(n) * (np.sin(2 * np.pi * 1.5 * t) > 0.7)
wav('p2', np.clip(m, -0.9, 0.9), fs)
bursts = np.zeros(n)
for o in range(2000, n - 3000, 7000): bursts[o:o + 400] = 0.6 * rng.standard_normal(400) * np.exp(-np.arange(400) / 90)
wav('p3', np.clip(0.6 * sp24 + bursts, -0.9, 0.9), fs)
PY
for n in p1 p2 p3; do
  "$ENC" -ifile:"$W/$n.wav" -ofile:"$W/$n.es" -aot:42 -usac:2 -br:24000 -esbr:1 -ccfl_idx:3 -pvc_enc:1 >/dev/null
  python3 -I - "$W" "$n" 24 <<'PY'
import re, struct, sys
W, n, keep = sys.argv[1], sys.argv[2], int(sys.argv[3])
es = open("%s/%s.es" % (W, n), 'rb').read(); txt = open("%s/%s.txt" % (W, n)).read()
cfg = int(re.search(r'dec_info_init:(\d+)', txt).group(1)); sizes = [int(x) for x in re.findall(r'ia_mp4_stsz_size:(\d+)', txt)][:keep]
open("%s/%s.asc" % (W, n), 'wb').write(es[:cfg]); p = cfg; out = b''
for s in sizes: out += struct.pack('>I', s) + es[p:p + s]; p += s
open("%s/%s.aus" % (W, n), 'wb').write(out)
PY
  python3 -I tools/mux_raw_aus_mp4.py "$W/$n" "tests/data/usac_$n.m4a" 1024
done
