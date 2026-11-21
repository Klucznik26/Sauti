#!/usr/bin/env bash
# AAC LTP (object type 4, tests/data/ltp_*) against two independent decoders.
#   FFmpeg (float, ~135-138 dB, lag 0, same length) on EVERY file: the long-term prediction, the forward MDCT of the predicted signal, the analysis TNS filter and the LTP buffer (incl. after
#     EIGHT_SHORT and start / stop frames) agree to float rounding.
#   FAAD2 (libfaad.so.2 through tools/sauti_faad_raw_oracle.c) only as a coarse second opinion, ~70-82 dB: its long-term buffer is coarser, so the bound is LTP_FAAD_MIN_DB (default 55; a structural
#     difference gives < 20 dB).  Used on files up to 32 kHz (below that FAAD2 enables implicit SBR and doubles the output rate).
# bounds: LTP_MIN_DB (default 130).  SKIP, stated loudly, exit 0 without ffmpeg / numpy / libfaad: that is not a proof of agreement.
# usage: scripts/test_aac_ltp_vs_ffmpeg.sh
set -euo pipefail
cd "$(dirname "$0")/.."
MIN="${LTP_MIN_DB:-130}"; FMIN="${LTP_FAAD_MIN_DB:-55}"
command -v ffmpeg >/dev/null || { echo "SKIP: no ffmpeg"; exit 0; }
python3 -c 'import numpy' 2>/dev/null || { echo "SKIP: no numpy"; exit 0; }
make -s build/maktabasauti_cli >/dev/null
HAVE_FAAD=0
if python3 -c 'import ctypes.util,sys; sys.exit(0 if ctypes.util.find_library("faad") else 1)'; then cc -O2 -o build/sauti_faad_raw_oracle tools/sauti_faad_raw_oracle.c -ldl && HAVE_FAAD=1; else echo "NOTE: no libfaad: FAAD2 comparison skipped (FFmpeg only)"; fi
W="$(mktemp -d "${TMPDIR:-/tmp}/sauti-ltp.XXXXXX")"; trap 'rm -rf -- "$W"' EXIT
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
files="ltp_st.m4a ltp_st.aac ltp_st.loas ltp_mono.m4a ltp_long.m4a ltp_var.m4a $(cd tests/data && ls ltp_r[01][0-9].m4a)"
for f in $files; do
  ch=2; case "$f" in ltp_mono*|ltp_long*) ch=1;; esac
  build/maktabasauti_cli "tests/data/$f" "$W/s.sau" | sed "s/^/$f: /"
  ffmpeg -v error -y -i "tests/data/$f" -f f32le "$W/s.ff"; r="$(cmp "$W/s.sau" "$W/s.ff" "$ch" "$MIN" 0)"; echo "$f vs FFmpeg: $r"; case "$r" in OK*) ;; *) fail=1;; esac
  if [ "$HAVE_FAAD" = 1 ]; then
    case "$f" in
      *.aac|*.loas|ltp_r06*|ltp_r07*|ltp_r08*|ltp_r09*|ltp_r10*|ltp_r11*|ltp_r12*) ;;
      *) python3 -I tools/mp4_dump_aus.py "tests/data/$f" "$W/s" >/dev/null
         build/sauti_faad_raw_oracle "$W/s.asc" "$W/s.aus" "$W/s.faad" >/dev/null 2>&1
         r="$(cmp "$W/s.sau" "$W/s.faad" "$ch" "$FMIN" 2500)"; echo "$f vs FAAD2:  $r"; case "$r" in OK*) ;; *) fail=1;; esac ;;
    esac
  fi
done
exit $fail
