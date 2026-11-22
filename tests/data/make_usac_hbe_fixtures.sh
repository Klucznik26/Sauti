#!/usr/bin/env bash
# Regenerates tests/data/usac_h1..h4.m4a: eSBR 2:1 with the HARMONIC TRANSPOSER (bs_harmonicSBR, ISO/IEC 23003-3 7.5.3) from SYNTHETIC signals (harmonic combs with a little noise), made by the encoder of libxaac
# (Apache License 2.0, built OUTSIDE this repository, default ~/Sauti-tools/libxaac/build/xaacenc) with -harmonic_sbr:1.  The encoder writes sbrPatchingMode 0 with sbrPitchInBins 12 / 24 / 36 (the "cross product"
# branch of the transposer) and a few frames of patching mode 1.  h1: 24 kHz mono (transposer synthesis of 16 bands), h2: 16 kHz mono (20 bands, the matrix form), h3: 44.1 kHz stereo (20 bands), h4: mono LPD core
# with PVC and the transposer; h5 / h6: eSBR 4:1 (core 1024 at a quarter of the output rate, 48 kHz mono / 44.1 kHz stereo).  8:3 streams are not made by this encoder with the transposer (it ignores the flag).
# usage: tests/data/make_usac_hbe_fixtures.sh   (needs python3 + numpy and the libxaac encoder; XAAC=<dir with xaacenc> to override)
set -euo pipefail
cd "$(dirname "$0")/../.."
ENC="${XAAC:-$HOME/Sauti-tools/libxaac/build}/xaacenc"
[ -x "$ENC" ] || { echo "no libxaac encoder at $ENC"; exit 1; }
W="$(mktemp -d "${TMPDIR:-/tmp}/sauti-hbe.XXXXXX")"; trap 'rm -rf -- "${W:?}"' EXIT
mk() {   # name fs channels usac_mode bitrate ccfl_idx extra...
  local name=$1 fs=$2 ch=$3 mode=$4 br=$5 ccfl=$6; shift 6
  python3 -I - "$W" "$fs" "$ch" "$name" <<'PY'
import numpy as np, struct, sys
W, fs, ch, name = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
n = fs * 3; t = np.arange(n) / fs
def sig(f0s, seed):
    r = np.random.default_rng(seed)
    m = sum(0.06 / h * np.sin(2 * np.pi * f * h * t * (1 + 0.003 * np.sin(2 * np.pi * 0.5 * t))) for f in f0s for h in range(1, 24) if f * h < fs / 2)
    return np.clip(m + 0.03 * r.standard_normal(n) * (0.5 + 0.5 * np.sin(2 * np.pi * 1.3 * t)), -0.9, 0.9)
x = [sig((196, 247, 294, 330), 1)]
if ch == 2: x.append(sig((220, 277, 330), 2))
d = (np.stack(x, 1).reshape(-1) * 32767).astype('<i2').tobytes()
open("%s/%s.wav" % (W, name), 'wb').write(b'RIFF' + struct.pack('<I', 36 + len(d)) + b'WAVEfmt ' + struct.pack('<IHHIIHH', 16, 1, ch, fs, fs * 2 * ch, 2 * ch, 16) + b'data' + struct.pack('<I', len(d)) + d)
PY
  "$ENC" -ifile:"$W/$name.wav" -ofile:"$W/$name.es" -aot:42 -usac:$mode -br:$br -esbr:1 -ccfl_idx:$ccfl -harmonic_sbr:1 "$@" >/dev/null
  python3 -I - "$W" "$name" 24 <<'PY'
import re, struct, sys
W, n, keep = sys.argv[1], sys.argv[2], int(sys.argv[3])
es = open("%s/%s.es" % (W, n), 'rb').read(); txt = open("%s/%s.txt" % (W, n)).read()
cfg = int(re.search(r'dec_info_init:(\d+)', txt).group(1)); sizes = [int(x) for x in re.findall(r'ia_mp4_stsz_size:(\d+)', txt)][:keep]
open("%s/%s.asc" % (W, n), 'wb').write(es[:cfg]); p = cfg; out = b''
for s in sizes: out += struct.pack('>I', s) + es[p:p + s]; p += s
open("%s/%s.aus" % (W, n), 'wb').write(out)
PY
  python3 -I tools/mux_raw_aus_mp4.py "$W/$name" "tests/data/usac_$name.m4a" 2048
}
mk h1 24000 1 1 32000 3
mk h2 16000 1 1 24000 3
mk h3 44100 2 1 64000 3
mk h4 24000 1 2 24000 3 -pvc_enc:1
mk h5 48000 1 1 32000 4
mk h6 44100 2 1 48000 4
