#!/usr/bin/env bash
# The real-encoder USAC fixtures (tests/data/usac_x1..x4.m4a, made by libxaac from synthetic signals, see tests/data/make_usac_xaac_fixtures.sh) against libxaac's own decoder (xaacdec: Apache License 2.0, built
# OUTSIDE this repository, default ~/Sauti-tools/libxaac/build).  It outputs 16-bit PCM, our decoder float: the comparison allows a constant delay (searched, the decoders differ by 22 samples at 48 kHz) and requires
# USAC_XAAC_MIN_DB (default 65) over the whole file; the measured values are 72-83 dB (libxaac is fixed point).  Before AudioPreRoll was implemented these streams agreed at 4.7-30 dB: the eSBR noise sequence starts at
# the phase the two pre-roll access units leave it at.  libxaac's 16-bit output wraps around (no saturation) on a sample beyond full scale; such samples are left out of the comparison.  SKIP (exit 0, stated loudly) without xaacdec / numpy.
# x5-x7 are eSBR 8:3 (768-line core); x6 is compared on the left channel only (see below).  l1-l3 have the LPD core (ACELP / TCX, no SBR): the same comparison (the 16-bit output of libxaac limits it to about 78 dB).
# usage: scripts/test_usac_vs_xaac.sh
set -euo pipefail
cd "$(dirname "$0")/.."
XAAC="${XAAC:-$HOME/Sauti-tools/libxaac/build}"; MIN="${USAC_XAAC_MIN_DB:-65}"
[ -x "$XAAC/xaacdec" ] || { echo "SKIP: no libxaac decoder at $XAAC/xaacdec (this is NOT a proof of agreement)"; exit 0; }
python3 -c 'import numpy' 2>/dev/null || { echo "SKIP: no numpy"; exit 0; }
make -s build/sauti_usac_raw >/dev/null
W="$(mktemp -d "${TMPDIR:-/tmp}/sauti-xaacdec.XXXXXX")"; trap 'rm -rf -- "$W"' EXIT
fail=0
for n in x1 x2 x3 x4 x5 x6 x7 l1 l2 l3; do
  ch=1; ts=24000; fl=1024; cmpc=all                                            # channels, time scale (core rate), core frame length, channels compared
  case "$n" in x3|x4) ch=2;; x5) ch=2; ts=18000; fl=768;; x6) ch=2; ts=16537; fl=768; cmpc=0;; x7) ts=12000; fl=768;; l1|l3) ts=16000;; l2) ts=24000;; esac
  python3 -I tools/mp4_dump_aus.py "tests/data/usac_$n.m4a" "$W/$n" >/dev/null
  python3 -I - "$W" "$n" "$ts" "$fl" <<'PY'
import struct, sys
W, n, ts, fl = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
asc = open("%s/%s.asc" % (W, n), 'rb').read(); raw = open("%s/%s.aus" % (W, n), 'rb').read(); p = 0; aus = []
while p + 4 <= len(raw):
    l = struct.unpack('>I', raw[p:p + 4])[0]; aus.append(raw[p + 4:p + 4 + l]); p += 4 + l
open("%s/%s.es" % (W, n), 'wb').write(asc + b''.join(aus))
t = ['-dec_info_init:%d' % len(asc), '-g_track_count:1', '-ia_mp4_stsz_entries:%d' % len(aus), '-movie_time_scale:%d' % ts, '-media_time_scale:%d' % ts, '-playTimeInSamples:%d' % (len(aus) * fl), '-startOffsetInSamples:0', '-useEditlist:1']
open("%s/%s.txt" % (W, n), 'w').write('\n'.join(t + ['-ia_mp4_stsz_size:%d' % len(a) for a in aus]) + '\n')
PY
  build/sauti_usac_raw "$W/$n" "$W/$n.sau" 2>/dev/null
  "$XAAC/xaacdec" -ifile:"$W/$n.es" -imeta:"$W/$n.txt" -mp4:1 -ofile:"$W/$n.pcm" -pcmsz:16 >/dev/null 2>&1 || true
  r="$(python3 -I - "$W" "$n" "$ch" "$MIN" "$cmpc" <<'PY'
import sys, numpy as np
W, n, ch, mn, cmpc = sys.argv[1], sys.argv[2], int(sys.argv[3]), float(sys.argv[4]), sys.argv[5]
try:
    o = np.fromfile("%s/%s.sau" % (W, n), dtype='<f4'); x = np.fromfile("%s/%s.pcm" % (W, n), dtype='<i2').astype(np.float32) / 32768.0
except Exception:
    print("FAIL no output"); sys.exit(0)
if len(o) == 0 or len(x) == 0: print("FAIL empty output"); sys.exit(0)
best = None
for d in range(-200 * ch, 200 * ch + 1, ch):
    a = o[max(0, d):]; b = x[max(0, -d):]; m = min(len(a), len(b)) - 4096
    if m < 8000: continue
    a2 = a[:m]; b2 = b[:m]
    if cmpc == '0' and ch == 2: a2 = a2[0::2]; b2 = b2[0::2]                  # x6 (m = 31 high-band channels): libxaac starts the noise sequence of the SECOND channel one frame (32 slots) behind - 32*m = 992 = 480 mod 512 indices - FDK and this decoder do not (FDK agrees with us at 84 dB on both channels of the 2:1 44.1 kHz stereo stream); the channels agree only when 32*m = 0 mod 512
    k = np.abs(a2) < 0.999                                                   # libxaac's 16-bit output WRAPS around on a sample beyond full scale instead of saturating: those samples are excluded
    e = float(np.sum((a2[k] - b2[k]) ** 2)); s = float(np.sum(b2[k] ** 2)); v = 999.0 if e == 0 else 10 * np.log10(s / e)
    if best is None or v > best[0]: best = (v, d // ch)
print("%s %.1f dB (delay %d samples)" % ("OK" if best and best[0] >= mn else "FAIL", best[0] if best else 0, best[1] if best else 0))
PY
)"
  echo "usac_$n vs libxaac: $r"; case "$r" in OK*) ;; *) fail=1;; esac
done
exit $fail
