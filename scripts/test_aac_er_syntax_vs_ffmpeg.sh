#!/usr/bin/env bash
# ER AAC LC (object type 17) and ER AAC LTP (19) against independent decoders (tests/data/erlc_*, erltp_*).
#   FFmpeg on every ER AAC LC file (mono / stereo / 960 lines / 3.0 / 5.1): ~136-139 dB, lag 0, same length.  FFmpeg REFUSES object type 19 (its ASC parser has no ER AAC LTP).
#   FAAD2 (libfaad.so.2 through tools/sauti_faad_raw_oracle.c): ER AAC LC stereo / mono ~136 dB (codec delay searched); ER AAC LTP mono only, ~67 dB (its long-term buffer is coarse, like for plain LTP);
#   FAAD2 fails on ER AAC LTP stereo, so those files are covered by the equivalence test in tests/test_aac_er_syntax.c (the same content in the plain syntax decodes to the identical PCM).
# bounds: ER_MIN_DB (default 130), ER_FAAD_MIN_DB (default 110), ER_FAAD_LTP_MIN_DB (default 55).  SKIP, stated loudly, exit 0 without ffmpeg / numpy / libfaad.
# usage: scripts/test_aac_er_syntax_vs_ffmpeg.sh
set -euo pipefail
cd "$(dirname "$0")/.."
MIN="${ER_MIN_DB:-130}"; FMIN="${ER_FAAD_MIN_DB:-110}"; LMIN="${ER_FAAD_LTP_MIN_DB:-55}"
command -v ffmpeg >/dev/null || { echo "SKIP: no ffmpeg"; exit 0; }
python3 -c 'import numpy' 2>/dev/null || { echo "SKIP: no numpy"; exit 0; }
make -s build/maktabasauti_cli >/dev/null
HAVE_FAAD=0
if python3 -c 'import ctypes.util,sys; sys.exit(0 if ctypes.util.find_library("faad") else 1)'; then cc -O2 -o build/sauti_faad_raw_oracle tools/sauti_faad_raw_oracle.c -ldl && HAVE_FAAD=1; else echo "NOTE: no libfaad: FAAD2 comparison skipped"; fi
W="$(mktemp -d "${TMPDIR:-/tmp}/sauti-ersyn.XXXXXX")"; trap 'rm -rf -- "$W"' EXIT
cmp() {  # cmp OURS.f32 THEIRS.f32 CHANNELS MIN_DB MAXLAG
  python3 -I - "$@" <<'PY'
import sys, numpy as np
a = np.fromfile(sys.argv[1], dtype='<f4'); b = np.fromfile(sys.argv[2], dtype='<f4'); ch = int(sys.argv[3]); mn = float(sys.argv[4]); maxlag = int(sys.argv[5])
a = a[:len(a) // ch * ch].reshape(-1, ch); b = b[:len(b) // ch * ch].reshape(-1, ch); best = None
for lag in range(-maxlag, maxlag + 1):
    if lag >= 0: x = a[lag:]; y = b[:len(a) - lag]
    else: y = b[-lag:]; x = a[:len(b) + lag]
    n = min(len(x), len(y)); x = x[:n][4000:n - 4000]; y = y[:n][4000:n - 4000]
    if len(x) < 1000: continue
    e = float(np.sum((x - y) ** 2)); s = float(np.sum(y ** 2)); snr = 999.0 if e == 0 else 10 * np.log10(s / e)
    if best is None or snr > best[0]: best = (snr, lag, len(x))
print("%s %.2f dB at lag %d over %d frames (lengths %d / %d)" % ("OK" if best and best[0] >= mn and (maxlag > 0 or len(a) == len(b)) else "FAIL", best[0], best[1], best[2], len(a), len(b)))
PY
}
fail=0
for f in erlc_st erlc_mono erlc960_st erlc_c3 erlc_c6 erlc_c7; do
  ch=2; case "$f" in *mono) ch=1;; *c3) ch=3;; *c6) ch=6;; *c7) ch=8;; esac
  build/maktabasauti_cli "tests/data/$f.m4a" "$W/s.sau" | sed "s/^/$f: /"
  ffmpeg -v error -y -i "tests/data/$f.m4a" -f f32le "$W/s.ff"; r="$(cmp "$W/s.sau" "$W/s.ff" "$ch" "$MIN" 0)"; echo "$f vs FFmpeg: $r"; case "$r" in OK*) ;; *) fail=1;; esac
  if [ "$HAVE_FAAD" = 1 ]; then case "$f" in erlc_st|erlc_mono)
    python3 -I tools/mp4_dump_aus.py "tests/data/$f.m4a" "$W/s" >/dev/null; build/sauti_faad_raw_oracle "$W/s.asc" "$W/s.aus" "$W/s.faad" >/dev/null 2>&1
    r="$(cmp "$W/s.sau" "$W/s.faad" "$ch" "$FMIN" 2500)"; echo "$f vs FAAD2:  $r"; case "$r" in OK*) ;; *) fail=1;; esac ;; esac; fi
done
if [ "$HAVE_FAAD" = 1 ]; then
  build/maktabasauti_cli tests/data/erltp_mono.m4a "$W/s.sau" | sed "s/^/erltp_mono: /"
  python3 -I tools/mp4_dump_aus.py tests/data/erltp_mono.m4a "$W/s" >/dev/null; build/sauti_faad_raw_oracle "$W/s.asc" "$W/s.aus" "$W/s.faad" >/dev/null 2>&1
  r="$(cmp "$W/s.sau" "$W/s.faad" 1 "$LMIN" 2500)"; echo "erltp_mono vs FAAD2: $r"; case "$r" in OK*) ;; *) fail=1;; esac
fi
exit $fail
