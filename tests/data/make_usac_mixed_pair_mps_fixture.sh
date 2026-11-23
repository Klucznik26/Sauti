#!/usr/bin/env bash
# Regenerates tests/data/usac_z14.m4a: like usac_z13 (a stereo USAC stream with eSBR 2:1 whose two core channels switch between FD and LPD independently: 3 access units with two FD channels, 22 with two LPD channels, 15 with one of each)
# but with MPEG Surround 212 (stereoConfigIndex 2: the two core channels are the downmix and the residual).  The core channel streams come from two mono streams of the libxaac encoder (Apache License 2.0, built OUTSIDE this
# repository, default ~/Sauti-tools/libxaac/build/xaacenc; -aot:42 -usac:0 -esbr:1 -ccfl_idx:3), the eSBR payload of the downmix and the Mps212Data of every pair from a STEREO MPS stream of the same settings (T: -mps:1 -tree_cfg:0;
# tools/usac_splice_pair.py), so the PCM is not that of the mono streams: the check is the agreement of independent decoders.  The END bits and the end of the side information were read once from the parser of the decoder.
# usage: tests/data/make_usac_mixed_pair_mps_fixture.sh   (needs python3 + numpy and the libxaac encoder; XAAC=<dir with xaacenc> to override)
set -euo pipefail
cd "$(dirname "$0")/../.."
ENC="${XAAC:-$HOME/Sauti-tools/libxaac/build}/xaacenc"
[ -x "$ENC" ] || { echo "no libxaac encoder at $ENC"; exit 1; }
W="$(mktemp -d "${TMPDIR:-/tmp}/sauti-mixedmps.XXXXXX")"; trap 'rm -rf -- "${W:?}"' EXIT
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
q = lambda x: (np.clip(x, -1, 1) * 32767).astype('<i2').astype(float)
L = q(sg); R = q(np.roll(sg, 40) * 0.8 + 0.02 * r.standard_normal(n))
def wav(name, x, ch):
    b = np.clip(x, -32768, 32767).astype('<i2').tobytes(); open("%s/%s.wav" % (W, name), 'wb').write(b'RIFF' + struct.pack('<I', 36 + len(b)) + b'WAVEfmt ' + struct.pack('<IHHIIHH', 16, 1, ch, fs, fs * 2 * ch, 2 * ch, 16) + b'data' + struct.pack('<I', len(b)) + b)
wav('A', L, 1); wav('B', np.roll(L, fs * 3 // 2), 1); wav('T', np.stack([L, R], 1).reshape(-1), 2)
PY
for n in A B; do "$ENC" -ifile:"$W/$n.wav" -ofile:"$W/$n.es" -aot:42 -usac:0 -esbr:1 -ccfl_idx:3 >/dev/null; done
"$ENC" -ifile:"$W/T.wav" -ofile:"$W/T.es" -aot:42 -usac:0 -esbr:1 -ccfl_idx:3 -mps:1 -tree_cfg:0 >/dev/null
cat > "$W/ends.json" <<'JSON'
{"A": [["L", 1468], ["L", 1356], ["L", 1356], ["L", 1356], ["L", 1356], ["L", 1356], ["L", 1356], ["L", 1356], ["L", 1356], ["F", 3851], ["F", 3513], ["F", 4166], ["F", 4143], ["F", 3204], ["F", 4342], ["F", 3836], ["F", 2788], ["F", 2665], ["F", 2708], ["F", 2348], ["F", 2422], ["F", 1892], ["F", 3795], ["F", 1744], ["F", 1710], ["F", 1739], ["F", 1748], ["L", 1826], ["L", 1327], ["L", 1305], ["L", 1119], ["L", 1299], ["L", 1156], ["L", 1293], ["L", 1243], ["L", 1332], ["L", 1156], ["L", 1095], ["L", 1146], ["L", 1182]],
 "B": [["L", 1356], ["L", 1356], ["L", 1356], ["L", 1422], ["L", 1454], ["L", 1445], ["L", 1427], ["L", 1418], ["L", 1356], ["F", 3270], ["F", 4131], ["F", 2927], ["L", 2240], ["L", 1287], ["L", 1172], ["L", 1118], ["L", 1134], ["L", 1135], ["L", 1174], ["L", 1151], ["L", 1108], ["L", 1123], ["L", 1105], ["L", 1222], ["L", 1252], ["L", 1126], ["L", 1165], ["L", 1145], ["L", 1103], ["L", 1087], ["L", 1301], ["L", 1251], ["L", 1184], ["L", 1311], ["L", 2087], ["L", 1506], ["L", 1356], ["L", 1356], ["L", 1356], ["L", 1356]],
 "T": {"ends": [["L", 2753], ["L", 2794], ["L", 2814], ["L", 2750], ["L", 2723], ["L", 2779], ["L", 2744], ["L", 2722], ["L", 2762], ["F", 5634], ["F", 4523], ["F", 4487], ["F", 4597], ["F", 3586], ["F", 3885], ["F", 4162], ["F", 3594], ["F", 3696], ["F", 3750], ["F", 3835], ["F", 3852], ["F", 4080], ["F", 3829], ["F", 3966], ["F", 3928], ["L", 2990], ["L", 2318], ["L", 2231], ["L", 2372], ["L", 2733], ["L", 2604], ["L", 2626], ["L", 2533], ["L", 2399], ["L", 2188], ["L", 2519], ["L", 2822], ["L", 2933], ["L", 2560], ["L", 2508]],
       "sbr": [2964, 2915, 2948, 2818, 2822, 2874, 2834, 2812, 2852, 5820, 4621, 4662, 4700, 3730, 3999, 4339, 3717, 3816, 3844, 3931, 3970, 4212, 3929, 4092, 4026, 3113, 2390, 2333, 2458, 2795, 2694, 2749, 2598, 2461, 2279, 2586, 2924, 3002, 2631, 2578]}}
JSON
python3 -I tools/usac_splice_pair.py "$W" A B T "$W/ends.json" 40 "$W/Z"
python3 -I tools/mux_raw_aus_mp4.py "$W/Z" tests/data/usac_z14.m4a 2048
