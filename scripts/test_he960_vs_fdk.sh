#!/usr/bin/env bash
# HE-AAC with 960 frames (tests/data/he960_mono.m4a, he960_stereo.m4a) against the Fraunhofer reference decoder (libfdk-aac built with 32-bit PCM: build/sauti_fdk_raw_oracle, which switches FDK's SBR QMF to
# HIGH QUALITY mode: by default FDK decodes the SBR of a stereo stream in LOW POWER mode and is 20 - 30 dB away from every other decoder).  Measured 70 - 85 dB (mono), 57 - 81 dB (stereo).  Bound
# HE960_FDK_MIN_DB (default 55).  SKIP (exit 0, said loudly) without the oracle / numpy.
set -euo pipefail
cd "$(dirname "$0")/.."
[ -x build/sauti_fdk_raw_oracle ] || { echo "SKIP: no build/sauti_fdk_raw_oracle (the FDK oracle) - this is NOT a proof of agreement"; exit 0; }
python3 -c 'import numpy' 2>/dev/null || { echo "SKIP: no numpy"; exit 0; }
make -s build/maktabasauti_cli >/dev/null
W="$(mktemp -d "${TMPDIR:-/tmp}/sauti-he960f.XXXXXX")"; trap 'rm -rf -- "$W"' EXIT
fail=0
for n in mono stereo; do
  python3 -I tools/mp4_dump_aus.py "tests/data/he960_$n.m4a" "$W/$n" >/dev/null
  build/maktabasauti_cli "tests/data/he960_$n.m4a" "$W/$n.sau" >/dev/null 2>&1
  build/sauti_fdk_raw_oracle "$W/$n.asc" "$W/$n.aus" "$W/$n.fdk" >/dev/null 2>&1
  r="$(python3 -I - "$W/$n" "$n" "${HE960_FDK_MIN_DB:-55}" <<'PY'
import sys, numpy as np
b, n, mn = sys.argv[1], sys.argv[2], float(sys.argv[3]); ch = 1 if n == 'mono' else 2
o = np.fromfile(b + '.sau', dtype='<f4').astype(np.float64); f = np.fromfile(b + '.fdk', dtype='<f4').astype(np.float64)
if len(o) == 0 or len(f) == 0: print("FAIL empty output"); sys.exit(0)
if n == 'mono' and len(f) == 2 * len(o): f = f[0::2]                  # FDK writes a mono stream as two identical channels
def lp(v, fc=3000):
    V = np.fft.rfft(v); fr = np.fft.rfftfreq(len(v), 1 / 48000.0); V[fr > fc] = 0; return np.fft.irfft(V, len(v))
A = lp(o[0::ch][20000:80000]); best = None
for lag in range(-4000, 4000):
    B = lp(f[0::ch][20000 + lag:80000 + lag])
    if len(B) != len(A): continue
    c = float((A * B).sum() / np.sqrt((A ** 2).sum() * (B ** 2).sum() + 1e-30))
    if best is None or c > best[0]: best = (c, lag)
lag = best[1] * ch; a = o[max(0, -lag):]; x = f[max(0, lag):]; m = min(len(a), len(x)); a = a[:m]; x = x[:m]
v = 999.0; txt = []
for c in range(ch):
    A_ = a[c::ch]; X_ = x[c::ch]; s = slice(20000, min(len(A_), 160000)); g = float((A_[s] * X_[s]).sum() / (X_[s] ** 2).sum()); X_ = X_ * g
    k = (np.abs(A_) < 0.999) & (np.abs(X_) < 0.999); k[:20000] = False
    vc = 10 * np.log10(float((X_[k] ** 2).sum()) / max(float(((A_ - X_)[k] ** 2).sum()), 1e-30)); v = min(v, vc); txt.append("%.1f" % vc)
print("%s %s dB (delay %d samples)" % ("OK" if v >= mn else "FAIL", "/".join(txt), best[1]))
PY
)"
  echo "he960_$n vs FDK: $r"; case "$r" in OK*) ;; *) fail=1;; esac
done
exit $fail
