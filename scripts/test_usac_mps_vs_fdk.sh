#!/usr/bin/env bash
# USAC MPEG Surround 212 against the Fraunhofer reference decoder (libfdk-aac, built with 32-bit PCM: tools/sauti_fdk_raw_oracle.c, build/sauti_fdk_raw_oracle), independent of libxaac.  The fixtures made by the libxaac ENCODER
# (usac_z1..z4: stereoConfigIndex 2, eSBR 2:1, z4 with residual bands) carry loudness metadata that FDK applies by default (a constant gain of 0.5012 = -6 dB: ours / FDK = 1.995), so the comparison fits ONE constant gain
# and requires USAC_MPS_FDK_MIN_DB (default 75; measured 82 dB).  The generator streams (usac_y1..y9: stereoConfigIndex 1 and 2, residual bands, phase coding; random eSBR data) have no such gain; they agree at about 40 dB
# (the random eSBR data limit the agreement of two independent decoders), bound USAC_MPS_FDK_GEN_MIN_DB (default 30).  usac_y11 (stereoConfigIndex 2 with STP) is NOT compared: for a prediction upmix with temporal shaping configured
# (even disabled in every frame; TSD streams are NOT affected: usac_y14 / y15 are compared) FDK's output is about 1/8 of ours and libxaac's level (an unexplained deviation of FDK; libxaac and this decoder agree to 3e-4 on the upmix).  SKIP (exit 0, stated loudly) without the oracle / numpy.
set -euo pipefail
cd "$(dirname "$0")/.."
[ -x build/sauti_fdk_raw_oracle ] || { echo "SKIP: no build/sauti_fdk_raw_oracle (the FDK oracle) - this is NOT a proof of agreement"; exit 0; }
python3 -c 'import numpy' 2>/dev/null || { echo "SKIP: no numpy"; exit 0; }
make -s build/sauti_usac_raw >/dev/null
W="$(mktemp -d "${TMPDIR:-/tmp}/sauti-mpsfdk.XXXXXX")"; trap 'rm -rf -- "${W:?}"' EXIT
fail=0
for f in z1 z2 z3 z4 z5 z6 z7 z8 z9 y1 y2 y3 y4 y5 y6 y7 y8 y9 y10 y14 y15 y16 y17 y18 y19 y20 y21 z10 z11 z12 z13 z14; do
  python3 -I tools/mp4_dump_aus.py "tests/data/usac_$f.m4a" "$W/$f" >/dev/null
  build/sauti_usac_raw "$W/$f" "$W/$f.sau" >/dev/null 2>&1
  build/sauti_fdk_raw_oracle "$W/$f.asc" "$W/$f.aus" "$W/$f.fdk" >/dev/null 2>&1
  case "$f" in z11|z13|z14) min="${USAC_MPS_FDK_MIXED_MIN_DB:-25}";; z9|z10|z12) min="${USAC_MPS_FDK_LPD_MIN_DB:-50}";; z*) min="${USAC_MPS_FDK_MIN_DB:-75}";; *) min="${USAC_MPS_FDK_GEN_MIN_DB:-30}";; esac
  r="$(python3 -I - "$W/$f.sau" "$W/$f.fdk" "$min" <<'PY'
import sys, numpy as np
a = np.fromfile(sys.argv[1], '<f4'); b = np.fromfile(sys.argv[2], '<f4'); lo = float(sys.argv[3]); m = min(len(a), len(b))
if m < 20000: print("FAIL too short"); sys.exit(0)
a = a[8192:m - 4096]; b = b[8192:m - 4096]; g = float((a * b).sum() / (b * b).sum()); e = ((a - g * b) ** 2).sum(); s = ((g * b) ** 2).sum()
v = 999.0 if e == 0 else 10 * np.log10(s / e)
print(("OK" if v >= lo else "FAIL"), "%.1f dB (gain ours/FDK %.4f, bound %.0f)" % (v, g, lo))
PY
)"
  echo "usac_$f vs FDK (MPS): $r"; case "$r" in OK*) ;; *) fail=1;; esac
done
exit $fail
