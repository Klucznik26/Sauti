#!/usr/bin/env bash
# HE-AAC with 960 frames (tests/data/he960_mono.m4a, he960_stereo.m4a) against libxaac's decoder (xaacdec, Apache 2.0, built outside; XAAC=<dir>): PCM of both, delay from the low band (libxaac drops
# its 2037-frame start-up), 16-bit truncation of ours as libxaac does, samples of full scale left out.  Bounds HE960_XAAC_MIN_DB (default 50; measured 55 - 75 dB).  FDK (the second reference) agrees with
# the MONO stream (70 - 85 dB; scripts/test_he960_vs_fdk.sh); for the stereo stream it differs from libxaac, FFmpeg and this decoder alike, also at 1024 (FFmpeg == ours 136 dB there): not used.
# SKIP (exit 0, said loudly) without xaacdec / numpy.
set -euo pipefail
cd "$(dirname "$0")/.."
XAAC="${XAAC:-$HOME/Sauti-tools/libxaac/build}"; MIN="${HE960_XAAC_MIN_DB:-50}"
[ -x "$XAAC/xaacdec" ] || { echo "SKIP: no libxaac decoder at $XAAC/xaacdec (this is NOT a proof of agreement)"; exit 0; }
python3 -c 'import numpy' 2>/dev/null || { echo "SKIP: no numpy"; exit 0; }
make -s build/maktabasauti_cli >/dev/null
W="$(mktemp -d "${TMPDIR:-/tmp}/sauti-he960x.XXXXXX")"; trap 'rm -rf -- "$W"' EXIT
fail=0
for n in mono stereo; do
  python3 -I tools/mp4_dump_aus.py "tests/data/he960_$n.m4a" "$W/$n" >/dev/null
  python3 -I - "$W" "$n" <<'PY'
import struct, sys
W, n = sys.argv[1], sys.argv[2]
asc = open("%s/%s.asc" % (W, n), 'rb').read(); raw = open("%s/%s.aus" % (W, n), 'rb').read(); p = 0; aus = []
while p + 4 <= len(raw):
    l = struct.unpack('>I', raw[p:p + 4])[0]; aus.append(raw[p + 4:p + 4 + l]); p += 4 + l
open("%s/%s.es" % (W, n), 'wb').write(asc + b''.join(aus))
t = ['-dec_info_init:%d' % len(asc), '-g_track_count:1', '-ia_mp4_stsz_entries:%d' % len(aus), '-movie_time_scale:24000', '-media_time_scale:24000', '-playTimeInSamples:%d' % (len(aus) * 960), '-startOffsetInSamples:0', '-useEditlist:1']
open("%s/%s.txt" % (W, n), 'w').write('\n'.join(t + ['-ia_mp4_stsz_size:%d' % len(a) for a in aus]) + '\n')
PY
  build/maktabasauti_cli "tests/data/he960_$n.m4a" "$W/$n.sau" >/dev/null 2>&1
  "$XAAC/xaacdec" -ifile:"$W/$n.es" -imeta:"$W/$n.txt" -mp4:1 -ofile:"$W/$n.pcm" -pcmsz:16 >/dev/null 2>&1 || true
  r="$(python3 -I - "$W" "$n" "$MIN" <<'PY'
import sys, numpy as np
W, n, mn = sys.argv[1], sys.argv[2], float(sys.argv[3]); ch = 1 if n == 'mono' else 2
try:
    o = np.fromfile("%s/%s.sau" % (W, n), dtype='<f4').astype(np.float64); x = np.fromfile("%s/%s.pcm" % (W, n), dtype='<i2').astype(np.float64) / 32768.0
except Exception:
    print("FAIL no output"); sys.exit(0)
if len(o) == 0 or len(x) == 0: print("FAIL empty output"); sys.exit(0)
if n == 'mono': x = x[0::2]                                       # libxaac writes a mono stream as two identical channels
o = np.trunc(o * 32768.0) / 32768.0
def lp(v, fc=3000):
    V = np.fft.rfft(v); f = np.fft.rfftfreq(len(v), 1 / 48000.0); V[f > fc] = 0; return np.fft.irfft(V, len(v))
A = lp(o[0::ch][10000:60000]); best = None
for lag in range(-5000, 5000):
    B = lp(x[0::ch][10000 + lag:60000 + lag])
    if len(B) != len(A): continue
    c = float((A * B).sum() / np.sqrt((A ** 2).sum() * (B ** 2).sum() + 1e-30))
    if best is None or c > best[0]: best = (c, lag)
if best is None: print("FAIL no lag"); sys.exit(0)
lag = best[1] * ch
a = o[max(0, -lag):]; b = x[max(0, lag):]; m = min(len(a), len(b)); a = a[:m]; b = b[:m]
sk = 6000 * ch                                                    # the first frames: libxaac start-up
a = a[sk:]; b = b[sk:]; k = (np.abs(a) < 0.999) & (np.abs(b) < 0.999)
v = 10 * np.log10(float((b[k] ** 2).sum()) / max(float(((a - b)[k] ** 2).sum()), 1e-30))
print("%s %.1f dB (delay %d samples)" % ("OK" if v >= mn else "FAIL", v, best[1]))
PY
)"
  echo "he960_$n vs libxaac: $r"; case "$r" in OK*) ;; *) fail=1;; esac
done
exit $fail
