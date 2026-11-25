#!/usr/bin/env bash
# Regenerates tests/data/usac_mb.m4a: USAC (xHE-AAC, eSBR 2:1) with uniDRC MULTI-BAND gain sets, made by libxaac's encoder (built OUTSIDE the repository: XAAC=<dir with xaacenc>, default
# ~/Sauti-tools/libxaac/build) from a SYNTHETIC stereo signal; config: gain set 0 = 2 bands (start sub band index 0 / 8), set 1 = 1 band, set 2 = 3 bands (0 / 4 / 16), instructions with the
# effects 0x0001 / 0x0800 / 0x0800.  The libxaac DECODER applies real multi-band weights, FDK applies the gain of band 0 to the whole spectrum (its "single-band DRC"), see docs/UNIDRC_PLAN.md.
set -euo pipefail
cd "$(dirname "$0")"
XAAC="${XAAC:-$HOME/Sauti-tools/libxaac/build}"; SRC="${SRC:-$HOME/Sauti-tools/libxaac/test/encoder/impd_drc_config_params.txt}"
W="$(mktemp -d "${TMPDIR:-/tmp}/sauti-usacmb.XXXXXX")"; trap 'rm -rf -- "${W:?}"' EXIT
python3 -I - "$W" "$SRC" <<'PY'
import numpy as np, sys, wave
W, src = sys.argv[1:3]; fs = 48000; n = fs * 2; t = np.arange(n) / fs; rng = np.random.default_rng(20261125)
h = sum(0.1 / k * np.sin(2 * np.pi * (180 + 40 * np.sin(2 * np.pi * 0.9 * t)) * k * t) for k in range(1, 50)) * 0.5
bursts = ((np.sin(2 * np.pi * 2.5 * t) > 0.75)).astype(float)
b = 0.2 * rng.standard_normal(n) * np.convolve(bursts, np.ones(900) / 900, 'same')
left = h + b + 0.08 * np.sin(2 * np.pi * (1500 * t + 1500 * t * t)); right = 0.7 * h + 0.6 * b[::-1]
x = np.stack([left, right], 1); x = x / np.abs(x).max() * 0.5
w = wave.open(W + '/in.wav', 'wb'); w.setnchannels(2); w.setsampwidth(2); w.setframerate(fs); w.writeframes((x * 32767).astype('<i2').tobytes()); w.close()
s = open(src).read(); a = s.index('#s=0\nband_count:1'); z = s.index('#####str_drc_instructions_uni_drc_v1#####')
def band(pts, st=None):
    r = 'nb_points:%d\n' % len(pts) + ''.join('x:%s\ny:%s\n' % p for p in pts) + 'width:0.01\nattack:0.01\ndecay:5.0\n'
    return r + ('start_sub_band_index:%d\n' % st if st is not None else '')
new = ('#s=0\nband_count:2\n#gain parameters m=0\n' + band([('-60.0', '-40.0'), ('-30.0', '-30.0'), ('0.0', '-20.0')], 0) + '#gain parameters m=1\n' + band([('-60.0', '-60.0'), ('0.0', '-5.0')], 8) +
       '#s=1\nband_count:1\n#gain parameters m=0\n' + band([('-50.0', '-50.0'), ('0.0', '-10.0')]) +
       '#s=2\nband_count:3\n#gain parameters m=0\n' + band([('-60.0', '-60.0'), ('-20.0', '-15.0'), ('0.0', '-5.0')], 0) + '#gain parameters m=1\n' + band([('-60.0', '-50.0'), ('0.0', '-12.0')], 4) +
       '#gain parameters m=2\n' + band([('-60.0', '-60.0'), ('0.0', '-2.0')], 16))
open(W + '/impd_drc_config_params.txt', 'w').write(s[:a] + new + s[z:])
PY
( cd "$W" && "$XAAC/xaacenc" -ifile:in.wav -ofile:mb.es -br:64000 -aot:42 -usac:1 -drc:1 >/dev/null )
python3 -I - "$W" <<'PY'
import sys, struct, re
W = sys.argv[1]; es = open(W + '/mb.es', 'rb').read(); txt = open(W + '/mb.txt').read()
n0 = int(re.search(r'-dec_info_init:(\d+)', txt).group(1)); sizes = [int(x) for x in re.findall(r'-ia_mp4_stsz_size:(\d+)', txt)]
open(W + '/mb.asc', 'wb').write(es[:n0]); p = n0; aus = b''
for n in sizes: aus += struct.pack('>I', n) + es[p:p + n]; p += n
open(W + '/mb.aus', 'wb').write(aus)
PY
python3 -I ../../tools/mux_raw_aus_mp4.py "$W/mb" usac_mb.m4a
