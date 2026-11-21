#!/usr/bin/env bash
# Regenerates tests/data/usac_x1..x4.m4a: REAL xHE-AAC streams made by an independent encoder, libxaac (https://github.com/ittiam-systems/libxaac, Apache License 2.0, built OUTSIDE this repository,
# default ~/Sauti-tools/libxaac/build/xaacenc) from SYNTHETIC signals generated below (white noise, a tone comb, harmonic series with noise bursts) - never from user material.  The repository carries only the OUTPUT
# streams; nothing of libxaac's code or tables is part of Sauti.  What they exercise that our own generator cannot: an encoder that really sets noise floors (the eSBR noise sequence), AudioPreRoll in the
# first access unit, the encoder's own choice of window sequences, TNS, scale factors and arithmetic coding contexts.
#   usac_x1  mono white noise, eSBR 2:1          usac_x2  mono tone comb, eSBR 2:1
#   usac_x3  stereo harmonic series + noise, eSBR 2:1          usac_x4  stereo harmonic series + noise, no SBR
# usage: tests/data/make_usac_xaac_fixtures.sh   (needs python3 + numpy and the libxaac encoder; XAAC=<dir with xaacenc> to override)
set -euo pipefail
cd "$(dirname "$0")"
ENC="${XAAC:-$HOME/Sauti-tools/libxaac/build}/xaacenc"
[ -x "$ENC" ] || { echo "no libxaac encoder at $ENC"; exit 1; }
W="$(mktemp -d "${TMPDIR:-/tmp}/sauti-xaac.XXXXXX")"; trap 'rm -rf -- "${W:?}"' EXIT
python3 -I - "$W" <<'PY'
import numpy as np, struct, sys
W = sys.argv[1]; fs = 48000; n = fs * 3 // 2; t = np.arange(n) / fs; rng = np.random.default_rng(20261121)
def wav(name, x, ch):
    d = (np.clip(x, -1, 1) * 32767).astype('<i2').tobytes()
    open("%s/%s.wav" % (W, name), 'wb').write(b'RIFF' + struct.pack('<I', 36 + len(d)) + b'WAVEfmt ' + struct.pack('<IHHIIHH', 16, 1, ch, fs, fs * 2 * ch, 2 * ch, 16) + b'data' + struct.pack('<I', len(d)) + d)
wav('x1', 0.2 * rng.standard_normal(n), 1)
wav('x2', sum(0.08 * np.sin(2 * np.pi * f * t) for f in (1300, 2100, 3900, 5300, 6700, 7900, 11000, 13500, 16000)), 1)
harm = sum(0.15 / h * np.sin(2 * np.pi * 220 * h * t) for h in range(1, 40)) * 0.5
burst = 0.15 * rng.standard_normal(n) * (np.sin(2 * np.pi * 3 * t) > 0.6)
st = np.stack([harm + burst, 0.8 * harm + 0.5 * burst[::-1]], axis=1).reshape(-1)
wav('x3', st, 2); wav('x4', st, 2)
PY
mk() {  # mk NAME FRAME_SAMPLES ENCODER_OPTIONS...
  local n="$1" fl="$2"; shift 2
  "$ENC" -ifile:"$W/$n.wav" -ofile:"$W/$n.es" -aot:42 -usac:1 "$@" >/dev/null
  python3 -I - "$W" "$n" <<'PY'
import re, struct, sys
W, n = sys.argv[1], sys.argv[2]
es = open("%s/%s.es" % (W, n), 'rb').read(); txt = open("%s/%s.txt" % (W, n)).read()
cfg = int(re.search(r'dec_info_init:(\d+)', txt).group(1)); sizes = [int(x) for x in re.findall(r'ia_mp4_stsz_size:(\d+)', txt)]
open("%s/%s.asc" % (W, n), 'wb').write(es[:cfg]); p = cfg; out = b''
for s in sizes: out += struct.pack('>I', s) + es[p:p + s]; p += s
assert p == len(es); open("%s/%s.aus" % (W, n), 'wb').write(out)
PY
  python3 -I ../../tools/mux_raw_aus_mp4.py "$W/$n" "usac_$n.m4a" "$fl"
}
mk x1 2048 -br:32000 -esbr:1
mk x2 2048 -br:32000 -esbr:1
mk x3 2048 -br:64000 -esbr:1
mk x4 1024 -br:96000 -esbr:0
