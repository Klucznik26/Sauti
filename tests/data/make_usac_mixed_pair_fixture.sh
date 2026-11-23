#!/usr/bin/env bash
# Regenerates tests/data/usac_z11.m4a: a USAC STEREO stream (24 kHz, core coder only, UsacChannelPairElement) whose two channels switch between FD and LPD INDEPENDENTLY: 4 of its 40 access units have two FD channels, 21 two LPD
# channels and 15 one of each (a channel pair of mixed core modes, which no encoder of the repository makes).  It is spliced from two mono streams of the libxaac encoder (Apache License 2.0, built OUTSIDE this repository,
# default ~/Sauti-tools/libxaac/build/xaacenc; -aot:42 -usac:0 -esbr:0 -ccfl_idx:1) by tools/usac_splice_pair.py; the two channels of a correct decoder are the PCM of the two mono streams (libxaac: to within one least significant bit).
# The END bits (where a channel stream stops) were read once from the parser of the decoder; they are fixed by the encoder's output for the synthetic signal below.
# usage: tests/data/make_usac_mixed_pair_fixture.sh   (needs python3 + numpy and the libxaac encoder; XAAC=<dir with xaacenc> to override)
set -euo pipefail
cd "$(dirname "$0")/../.."
ENC="${XAAC:-$HOME/Sauti-tools/libxaac/build}/xaacenc"
[ -x "$ENC" ] || { echo "no libxaac encoder at $ENC"; exit 1; }
W="$(mktemp -d "${TMPDIR:-/tmp}/sauti-mixed.XXXXXX")"; trap 'rm -rf -- "${W:?}"' EXIT
python3 -I - "$W" <<'PY'
import numpy as np, struct, sys
W = sys.argv[1]; fs = 48000; n = fs * 8; t = np.arange(n) / fs; r = np.random.default_rng(7); sg = np.zeros(n); seg = (t // 1.0).astype(int)
for k in range(8):                                                          # voiced segments (pitch glide, formant weighted harmonics), chords and noise bursts with clicks: the encoder switches between FD and LPD
    m = seg == k; tt = t[m]
    if k % 3 == 0:
        f0 = 120 + 40 * np.sin(2 * np.pi * 1.3 * tt); ph = np.cumsum(f0) / fs * 2 * np.pi
        x = sum(np.sin(h * ph) * np.exp(-((h * f0 - 700) / 400) ** 2) + 0.6 * np.sin(h * ph) * np.exp(-((h * f0 - 1800) / 500) ** 2) for h in range(1, 30)) * 0.12 * (0.5 + 0.5 * np.sin(2 * np.pi * 4 * tt) ** 2)
    elif k % 3 == 1:
        x = sum(0.08 * np.sin(2 * np.pi * f * tt) for f in (261.6, 329.6, 392, 523.2, 659.3, 784))
    else:
        x = r.standard_normal(m.sum()) * 0.05 * (np.sin(2 * np.pi * 6 * tt) > 0.3); x[::3000] += 0.8
    sg[m] = x
q = lambda x: (np.clip(x, -1, 1) * 32767).astype('<i2').astype(float)                 # 16 bit samples at 48 kHz, then decimated to 24 kHz
L = q(sg); R = q(np.roll(sg, 40) * 0.8 + 0.02 * r.standard_normal(n))
def half(x): return np.interp(np.arange(len(x) // 2) * 2.0, np.arange(len(x)), np.convolve(x, np.ones(4) / 4, 'same'))   # 24 kHz
def wav(name, x, ch):
    b = np.clip(x, -32768, 32767).astype('<i2').tobytes(); open("%s/%s.wav" % (W, name), 'wb').write(b'RIFF' + struct.pack('<I', 36 + len(b)) + b'WAVEfmt ' + struct.pack('<IHHIIHH', 16, 1, ch, 24000, 24000 * 2 * ch, 2 * ch, 16) + b'data' + struct.pack('<I', len(b)) + b)
l = half(L); r_ = half(R)
wav('A', l, 1); wav('B', np.roll(l, 24000 * 3 // 2), 1); wav('T', np.stack([l, r_], 1).reshape(-1), 2)
PY
for n in A B T; do "$ENC" -ifile:"$W/$n.wav" -ofile:"$W/$n.es" -aot:42 -usac:0 -esbr:0 -ccfl_idx:1 >/dev/null; done
cat > "$W/ends.json" <<'JSON'
{"A": [["L", 1361], ["L", 1356], ["L", 1356], ["L", 1356], ["L", 1356], ["L", 1491], ["L", 1356], ["L", 1356], ["L", 1356], ["F", 2989], ["F", 3649], ["F", 4214], ["F", 4759], ["F", 3269], ["F", 4175], ["F", 4632], ["F", 3768], ["F", 2841], ["F", 2991], ["F", 2712], ["F", 2687], ["F", 2483], ["F", 4241], ["F", 2835], ["F", 2162], ["F", 2161], ["F", 2194], ["F", 2167], ["L", 1487], ["L", 1136], ["L", 1119], ["L", 1193], ["L", 1136], ["L", 1127], ["L", 1132], ["L", 1242], ["L", 1129], ["L", 1196], ["L", 1265], ["L", 1120]],
 "B": [["L", 1361], ["L", 1434], ["L", 1356], ["L", 1418], ["L", 1417], ["L", 1539], ["L", 1561], ["L", 1440], ["L", 1356], ["F", 2393], ["F", 4391], ["F", 3513], ["L", 2242], ["L", 1166], ["F", 3458], ["L", 1664], ["L", 1307], ["L", 1178], ["L", 1186], ["L", 1114], ["L", 1159], ["L", 1104], ["L", 1175], ["L", 1158], ["L", 1152], ["L", 1265], ["L", 1167], ["L", 1159], ["L", 1094], ["L", 1134], ["L", 1129], ["L", 1124], ["L", 1137], ["L", 1173], ["L", 1124], ["L", 1689], ["L", 1356], ["L", 1356], ["L", 1356], ["L", 1356]]}
JSON
python3 -I tools/usac_splice_pair.py "$W" A B T "$W/ends.json" 40 "$W/Z"
python3 -I tools/mux_raw_aus_mp4.py "$W/Z" tests/data/usac_z11.m4a 1024
