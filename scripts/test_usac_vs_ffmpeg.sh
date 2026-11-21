#!/usr/bin/env bash
# USAC (xHE-AAC) frequency-domain streams (tests/data/usac_*.m4a, tools/sauti_usac_gen.c) against two independent decoders.
#   FFmpeg (float; 125-139 dB; the first frame of every stream has the sine window: FDK takes the first frame's own window shape as the previous one, FFmpeg sine) on the streams whose tools it implements the way the standard does: it seeds the noise generator of the first channel with 0 (FDK and the standard: 0x3039), skips noise
#   filling at noise_level 0 (the generator still advances) and never applies the TNS of independent windows, so usac_m3 and usac_s2 are not compared with it.
#   FDK AAC 32-bit (tools/sauti_fdk_raw_oracle.c, see its header; limiter off): ~90 dB at best (its noise floor is about -143 dB RMS), every stream.  Known FDK 2.0.3 defect: with tns_active = 1, tns_on_lr = 0 and M/S it forgets to update the spectrum exponent after the M/S
#   (left x0.5, right scaled by its old exponent; docs/AAC_FAMILY_ASM.md SBR-105), the fixtures avoid that combination for FDK.
# bounds: USAC_MIN_DB (FFmpeg, default 120), USAC_FDK_MIN_DB (default 80).  SKIP (exit 0, stated loudly) without ffmpeg / numpy / the FDK oracle.
# usage: scripts/test_usac_vs_ffmpeg.sh
set -euo pipefail
cd "$(dirname "$0")/.."
MIN="${USAC_MIN_DB:-120}"; FMIN="${USAC_FDK_MIN_DB:-80}"
command -v ffmpeg >/dev/null || { echo "SKIP: no ffmpeg"; exit 0; }
python3 -c 'import numpy' 2>/dev/null || { echo "SKIP: no numpy"; exit 0; }
make -s build/sauti_usac_raw >/dev/null
W="$(mktemp -d "${TMPDIR:-/tmp}/sauti-usac.XXXXXX")"; trap 'rm -rf -- "$W"' EXIT
cmp() {  # cmp OURS.f32 THEIRS.f32 CHANNELS MIN_DB
  python3 -I - "$@" <<'PY'
import sys, numpy as np
a = np.fromfile(sys.argv[1], dtype='<f4'); b = np.fromfile(sys.argv[2], dtype='<f4'); ch = int(sys.argv[3]); mn = float(sys.argv[4])
n = min(len(a), len(b)) // ch * ch; x = a[:n]; y = b[:n]
e = float(np.sum((x - y) ** 2)); s = float(np.sum(y ** 2)); snr = 999.0 if e == 0 else 10 * np.log10(s / e)
print("%s %.2f dB (lengths %d / %d, peak %.3f)" % ("OK" if snr >= mn and len(a) == len(b) and s > 0 else "FAIL", snr, len(a), len(b), float(np.abs(y).max())))
PY
}
fail=0
for f in m1 m2 m3 m4 m5 s1 s2 s3 s4 c1 c2 c3 c4 c5; do
  ch=1; case "$f" in s*) ch=2;; esac
  python3 -I tools/mp4_dump_aus.py "tests/data/usac_$f.m4a" "$W/$f" >/dev/null
  build/sauti_usac_raw "$W/$f" "$W/$f.sau" 2>/dev/null
  case "$f" in
    m3|m5|s2|s4) echo "usac_$f vs FFmpeg: not compared (FFmpeg's noise filling / TNS of independent windows differ from the standard)" ;;
    c1|c2|c3|c4|c5) echo "usac_$f vs FFmpeg: not compared (complex prediction: FFmpeg builds the downmix of the predicted bands the wrong way round and has no previous-frame MDST filter)" ;;
    *) ffmpeg -v error -y -i "tests/data/usac_$f.m4a" -f f32le "$W/$f.ff"; r="$(cmp "$W/$f.sau" "$W/$f.ff" "$ch" "$MIN")"; echo "usac_$f vs FFmpeg: $r"; case "$r" in OK*) ;; *) fail=1;; esac ;;
  esac
  if [ -x build/sauti_fdk_raw_oracle ]; then
    build/sauti_fdk_raw_oracle "$W/$f.asc" "$W/$f.aus" "$W/$f.fdk" >/dev/null 2>&1
    r="$(cmp "$W/$f.sau" "$W/$f.fdk" "$ch" "$FMIN")"; echo "usac_$f vs FDK:    $r"; case "$r" in OK*) ;; *) fail=1;; esac
  else echo "NOTE: no build/sauti_fdk_raw_oracle: FDK comparison skipped"; fi
done
exit $fail
