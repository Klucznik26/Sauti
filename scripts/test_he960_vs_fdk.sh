#!/usr/bin/env bash
# HE-AAC with 960 frames, MONO fixture (tests/data/he960_mono.m4a) against the Fraunhofer reference decoder (libfdk-aac built with 32-bit PCM: build/sauti_fdk_raw_oracle).  FDK agrees with this decoder on the mono
# stream at 70 - 85 dB.  The STEREO stream is deliberately not compared: on streams of the libxaac ENCODER FDK differs from libxaac, FFmpeg and this decoder alike for stereo HE-AAC (also at 1024, where
# FFmpeg == this decoder at 136 dB), so it says nothing about 960.  Bound HE960_FDK_MIN_DB (default 60).  SKIP (exit 0, said loudly) without the oracle / numpy.
set -euo pipefail
cd "$(dirname "$0")/.."
[ -x build/sauti_fdk_raw_oracle ] || { echo "SKIP: no build/sauti_fdk_raw_oracle (the FDK oracle) - this is NOT a proof of agreement"; exit 0; }
python3 -c 'import numpy' 2>/dev/null || { echo "SKIP: no numpy"; exit 0; }
make -s build/maktabasauti_cli >/dev/null
W="$(mktemp -d "${TMPDIR:-/tmp}/sauti-he960f.XXXXXX")"; trap 'rm -rf -- "$W"' EXIT
python3 -I tools/mp4_dump_aus.py tests/data/he960_mono.m4a "$W/m" >/dev/null
build/maktabasauti_cli tests/data/he960_mono.m4a "$W/m.sau" >/dev/null 2>&1
build/sauti_fdk_raw_oracle "$W/m.asc" "$W/m.aus" "$W/m.fdk" >/dev/null 2>&1
r="$(python3 -I - "$W/m" "${HE960_FDK_MIN_DB:-60}" <<'PY'
import sys, numpy as np
b, mn = sys.argv[1], float(sys.argv[2])
o = np.fromfile(b + '.sau', dtype='<f4').astype(np.float64); f = np.fromfile(b + '.fdk', dtype='<f4').astype(np.float64)
if len(o) == 0 or len(f) == 0: print("FAIL empty output"); sys.exit(0)
if len(f) == 2 * len(o): f = f[0::2]                                # FDK writes a mono stream as two identical channels
def lp(v, fc=3000):
    V = np.fft.rfft(v); fr = np.fft.rfftfreq(len(v), 1 / 48000.0); V[fr > fc] = 0; return np.fft.irfft(V, len(v))
A = lp(o[20000:80000]); best = None
for lag in range(-4000, 4000):
    B = lp(f[20000 + lag:80000 + lag])
    if len(B) != len(A): continue
    c = float((A * B).sum() / np.sqrt((A ** 2).sum() * (B ** 2).sum() + 1e-30))
    if best is None or c > best[0]: best = (c, lag)
lag = best[1]; a = o[max(0, -lag):]; x = f[max(0, lag):]; m = min(len(a), len(x)); a = a[:m]; x = x[:m]
s = slice(20000, min(m, 160000)); g = float((a[s] * x[s]).sum() / (x[s] ** 2).sum()); x = x * g
k = (np.abs(a) < 0.999) & (np.abs(x) < 0.999); k[:20000] = False
v = 10 * np.log10(float((x[k] ** 2).sum()) / max(float(((a - x)[k] ** 2).sum()), 1e-30))
print("%s %.1f dB (delay %d samples, gain %.4f)" % ("OK" if v >= mn else "FAIL", v, lag, g))
PY
)"
echo "he960_mono vs FDK: $r"; case "$r" in OK*) exit 0;; *) exit 1;; esac
