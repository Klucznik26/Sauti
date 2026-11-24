#!/usr/bin/env bash
# Regenerates tests/data/eldsbr_single.m4a, eldsbr_dual.m4a and eldsbr_v2.m4a (ELD v2: mono core + low delay MPEG Surround): AAC-ELD with the LOW-DELAY SBR (CLDFB), single rate (core and output 48 kHz) and dual rate (core 24 kHz, output 48 kHz), made by the Fraunhofer FDK AAC encoder
# (built OUTSIDE the repository: FDK=<install dir with include/fdk-aac and lib>) from a SYNTHETIC stereo signal generated below (harmonic stack, noise bursts = transients, a rising tone = harmonic
# flags, left/right differences = coupled and uncoupled frames), wrapped into M4A by tools/mux_raw_aus_mp4.py.  usage: tests/data/make_eldsbr_fixtures.sh
set -euo pipefail
cd "$(dirname "$0")"
FDK="${FDK:-$HOME/Sauti-tools/fdk-full}"
W="$(mktemp -d "${TMPDIR:-/tmp}/sauti-eldsbr.XXXXXX")"; trap 'rm -rf -- "${W:?}"' EXIT
cc -O1 -I"$FDK/include/fdk-aac" -I"$FDK/include" -o "$W/enc" ../../tools/sauti_fdk_aac_encoder.c -L"$FDK/lib" -lfdk-aac -Wl,-rpath,"$FDK/lib" -lm
python3 -I - "$W" <<'PY'
import numpy as np, sys
W = sys.argv[1]; fs = 48000; n = fs * 6; t = np.arange(n) / fs; rng = np.random.default_rng(20261123)
h = sum(0.1 / k * np.sin(2 * np.pi * (180 + 40 * np.sin(2 * np.pi * 0.35 * t)) * k * t) for k in range(1, 70)) * 0.5
bursts = ((np.sin(2 * np.pi * 2.5 * t) > 0.75) | (np.sin(2 * np.pi * 0.9 * t) > 0.97)).astype(float)
b = 0.2 * rng.standard_normal(n) * np.convolve(bursts, np.ones(900) / 900, 'same')
sw = 0.08 * np.sin(2 * np.pi * (1500 * t + 1500 * t * t))
tone = 0.05 * np.sin(2 * np.pi * 9600 * t) * (np.sin(2 * np.pi * 0.5 * t) > 0)
left = h + b + sw + tone
right = 0.7 * h + 0.6 * b[::-1] + 0.6 * sw[::-1] + tone * (np.sin(2 * np.pi * 0.25 * t) > 0)
x = np.stack([left, right], axis=1); x = x / max(1e-9, np.abs(x).max()) * 0.5      # peak 0.5: no clipping in either decoder
open(W + '/in.raw', 'wb').write((x * 32767).astype('<i2').tobytes())
PY
mk() {  # mk NAME RATIO ASCBITS   (ASCBITS: the exact length of the ELD AudioSpecificConfig in bits as our parser reads it: 48 single rate, 53 dual rate; needed for the LATM StreamMuxConfig)
  SBRMODE=1 SBRRATIO="$2" "$W/enc" "$W/in.raw" 48000 2 39 48000 512 "$W/$1.aus" "$W/$1.asc" >/dev/null
  python3 -I ../../tools/mux_raw_aus_mp4.py "$W/$1" "eldsbr_$1.m4a" 512
  # the first 120 access units again as a LOAS / LATM stream (AAC-ELD with the low-delay SBR in LATM)
  python3 -I - "$W/$1" "$W/$1c" <<'PY'
import struct, sys
a, c = sys.argv[1:3]; raw = open(a + '.aus', 'rb').read(); p = 0; out = b''
for _ in range(120):
    n = struct.unpack('>I', raw[p:p + 4])[0]; out += raw[p:p + 4 + n]; p += 4 + n
open(c + '.aus', 'wb').write(out); open(c + '.asc', 'wb').write(open(a + '.asc', 'rb').read())
PY
  echo "$3" > "$W/$1c.ascbits"
  python3 -I ../../tools/mux_raw_aus_loas.py "$W/$1c" "eldsbr_$1.loas"
}
mk single 1 48
mk dual 2 53
# ELD v2: the stereo input is coded as a MONO core plus the low delay MPEG Surround 212 (CHMODE 128 = MODE_212), dual rate; 32 kbit/s
CHMODE=128 SBRMODE=1 SBRRATIO=2 "$W/enc" "$W/in.raw" 48000 2 39 32000 512 "$W/v2.aus" "$W/v2.asc" >/dev/null
python3 -I ../../tools/mux_raw_aus_mp4.py "$W/v2" "eldsbr_v2.m4a" 512
