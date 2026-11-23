#!/usr/bin/env bash
# Regenerates tests/data/he960_mono.m4a and he960_stereo.m4a: HE-AAC (AAC-LC 960 + SBR, DAB+ framing, implicit signalling in a 2-byte AudioSpecificConfig, frameLengthFlag = 1) made by the libxaac encoder
# (Apache 2.0, built OUTSIDE the repository; XAAC=<dir with xaacenc>) from SYNTHETIC signals generated below, wrapped into M4A by tools/mux_raw_aus_mp4.py.  usage: tests/data/make_he960_fixtures.sh
set -euo pipefail
cd "$(dirname "$0")"
ENC="${XAAC:-$HOME/Sauti-tools/libxaac/build}/xaacenc"
[ -x "$ENC" ] || { echo "no libxaac encoder at $ENC"; exit 1; }
W="$(mktemp -d "${TMPDIR:-/tmp}/sauti-he960.XXXXXX")"; trap 'rm -rf -- "${W:?}"' EXIT
python3 -I - "$W" <<'PY'
import numpy as np, struct, sys
W = sys.argv[1]; fs = 48000; n = fs * 4; t = np.arange(n) / fs; rng = np.random.default_rng(20261123)
def wav(name, x, ch):
    d = (np.clip(x, -1, 1) * 32767).astype('<i2').tobytes()
    open("%s/%s.wav" % (W, name), 'wb').write(b'RIFF' + struct.pack('<I', 36 + len(d)) + b'WAVEfmt ' + struct.pack('<IHHIIHH', 16, 1, ch, fs, fs * 2 * ch, 2 * ch, 16) + b'data' + struct.pack('<I', len(d)) + d)
h = sum(0.1 / k * np.sin(2 * np.pi * 220 * k * t) for k in range(1, 40)) * 0.5
b = 0.1 * rng.standard_normal(n) * (np.sin(2 * np.pi * 3 * t) > 0.6)
wav('mono', h + b, 1)
wav('stereo', np.stack([h + b, 0.8 * h + 0.5 * b[::-1]], axis=1).reshape(-1), 2)
PY
mk() {  # mk NAME BITRATE
  local n="$1"
  "$ENC" -ifile:"$W/$n.wav" -ofile:"$W/$n.es" -aot:5 -br:"$2" -framesize:960 >/dev/null 2>&1
  python3 -I - "$W" "$n" <<'PY'
import re, struct, sys
W, n = sys.argv[1], sys.argv[2]
es = open("%s/%s.es" % (W, n), 'rb').read(); txt = open("%s/%s.txt" % (W, n)).read()
cfg = int(re.search(r'dec_info_init:(\d+)', txt).group(1)); sizes = [int(x) for x in re.findall(r'ia_mp4_stsz_size:(\d+)', txt)]
open("%s/%s.asc" % (W, n), 'wb').write(es[:cfg]); p = cfg; out = b''
for s in sizes: out += struct.pack('>I', s) + es[p:p + s]; p += s
assert p == len(es); open("%s/%s.aus" % (W, n), 'wb').write(out)
PY
  python3 -I ../../tools/mux_raw_aus_mp4.py "$W/$n" "he960_$n.m4a" 960
}
mk mono 32000
mk stereo 48000
