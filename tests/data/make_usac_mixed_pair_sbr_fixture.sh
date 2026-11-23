#!/usr/bin/env bash
# Regenerates tests/data/usac_z13.m4a: like usac_z11 (a stereo USAC stream whose channels switch between FD and LPD independently: 3 access units with two FD channels, 22 with two LPD channels, 15 with one of each) but WITH eSBR 2:1
# (48 kHz output, 24 kHz core).  The core channel streams come from two mono streams of the libxaac encoder (Apache License 2.0, built OUTSIDE this repository, default ~/Sauti-tools/libxaac/build/xaacenc; -aot:42 -usac:0 -esbr:1
# -ccfl_idx:3), the eSBR payload of every pair from a STEREO stream of the same settings (tools/usac_splice_pair.py, "T"): the PCM is therefore not that of the mono streams, the check is the agreement of independent decoders:
# libxaac (channel 0 only: its second channel starts the eSBR noise sequence one frame behind, as for usac_x6) and FDK.  The END bits and the end of the eSBR payload were read once from the parser of the decoder.
# usage: tests/data/make_usac_mixed_pair_sbr_fixture.sh   (needs python3 + numpy and the libxaac encoder; XAAC=<dir with xaacenc> to override)
set -euo pipefail
cd "$(dirname "$0")/../.."
ENC="${XAAC:-$HOME/Sauti-tools/libxaac/build}/xaacenc"
[ -x "$ENC" ] || { echo "no libxaac encoder at $ENC"; exit 1; }
W="$(mktemp -d "${TMPDIR:-/tmp}/sauti-mixedsbr.XXXXXX")"; trap 'rm -rf -- "${W:?}"' EXIT
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
for n in A B T; do "$ENC" -ifile:"$W/$n.wav" -ofile:"$W/$n.es" -aot:42 -usac:0 -esbr:1 -ccfl_idx:3 >/dev/null; done
cat > "$W/ends.json" <<'JSON'
{"A": [["L", 1468], ["L", 1356], ["L", 1356], ["L", 1356], ["L", 1356], ["L", 1356], ["L", 1356], ["L", 1356], ["L", 1356], ["F", 3851], ["F", 3513], ["F", 4166], ["F", 4143], ["F", 3204], ["F", 4342], ["F", 3836], ["F", 2788], ["F", 2665], ["F", 2708], ["F", 2348], ["F", 2422], ["F", 1892], ["F", 3795], ["F", 1744], ["F", 1710], ["F", 1739], ["F", 1748], ["L", 1826], ["L", 1327], ["L", 1305], ["L", 1119], ["L", 1299], ["L", 1156], ["L", 1293], ["L", 1243], ["L", 1332], ["L", 1156], ["L", 1095], ["L", 1146], ["L", 1182]],
 "B": [["L", 1356], ["L", 1356], ["L", 1356], ["L", 1422], ["L", 1454], ["L", 1445], ["L", 1427], ["L", 1418], ["L", 1356], ["F", 3270], ["F", 4131], ["F", 2927], ["L", 2240], ["L", 1287], ["L", 1172], ["L", 1118], ["L", 1134], ["L", 1135], ["L", 1174], ["L", 1151], ["L", 1108], ["L", 1123], ["L", 1105], ["L", 1222], ["L", 1252], ["L", 1126], ["L", 1165], ["L", 1145], ["L", 1103], ["L", 1087], ["L", 1301], ["L", 1251], ["L", 1184], ["L", 1311], ["L", 2087], ["L", 1506], ["L", 1356], ["L", 1356], ["L", 1356], ["L", 1356]],
 "T": {"ends": [["L", 2849], ["L", 2745], ["L", 2761], ["L", 2714], ["L", 2706], ["L", 2754], ["L", 2741], ["L", 2706], ["L", 2750], ["F", 5249], ["F", 4078], ["F", 4360], ["F", 4640], ["F", 3500], ["F", 4538], ["F", 3345], ["F", 3717], ["F", 3791], ["F", 3799], ["F", 3870], ["F", 3923], ["F", 4078], ["F", 3887], ["F", 3885], ["F", 3788], ["F", 3933], ["F", 4001], ["L", 3260], ["L", 2845], ["L", 2649], ["L", 2552], ["L", 2732], ["L", 2561], ["L", 2543], ["L", 2331], ["L", 2445], ["L", 2577], ["L", 2523], ["L", 2567], ["L", 2582]],
       "sbr": [2999, 2861, 2864, 2826, 2821, 2870, 2846, 2803, 2871, 5437, 4258, 4487, 4865, 3732, 4662, 3552, 3868, 3912, 3909, 3980, 4061, 4194, 3997, 4047, 3904, 4058, 4110, 3366, 2949, 2763, 2668, 2879, 2668, 2655, 2439, 2547, 2680, 2639, 2675, 2698]}}
JSON
python3 -I tools/usac_splice_pair.py "$W" A B T "$W/ends.json" 40 "$W/Z"
python3 -I tools/mux_raw_aus_mp4.py "$W/Z" tests/data/usac_z13.m4a 2048
