#!/usr/bin/env bash
# AAC Main (tests/data/main_*) against two independent decoders.
#   FFmpeg (float, ~138 dB, lag 0, same length) on every file with a CONSTANT max_sfb: FFmpeg keeps stale prediction_used flags for bands above max_sfb when max_sfb changes between frames
#     (main_var is therefore not compared with it: SNR ~36 dB there is FFmpeg's, not ours) and it does not reset the predictors of noise bands (main_pns).
#   FAAD2 (libfaad.so.2 through tools/sauti_faad_raw_oracle.c; float, ~135 dB, the codec delay is searched) on the files up to 32 kHz (below that FAAD2 turns on implicit SBR and doubles the
#     output rate): main_st, main_mono, main_long, main_var, main_pns (~118 dB: FAAD2's noise generator differs, the noise is 20 octaves quiet) and main_r00..r05.
# bounds: MAIN_MIN_DB (default 130), MAIN_FAAD_MIN_DB (default 110; 100 for main_pns).  SKIP, stated loudly, exit 0 without ffmpeg / numpy / libfaad: that is not a proof of agreement.
# main960_*: Main with 960-line frames (FFmpeg is the only decoder that has it; no FAAD2 comparison).
# Multichannel files (channel_configuration 3..7) go to FFmpeg only; the *_crc.aac files (ADTS with the error check) decode like the plain ADTS stream (FFmpeg ignores the check; the check
# itself is exercised by the test).
# usage: scripts/test_aac_main_vs_ffmpeg.sh
set -euo pipefail
cd "$(dirname "$0")/.."
MIN="${MAIN_MIN_DB:-130}"; FMIN="${MAIN_FAAD_MIN_DB:-110}"
command -v ffmpeg >/dev/null || { echo "SKIP: no ffmpeg"; exit 0; }
python3 -c 'import numpy' 2>/dev/null || { echo "SKIP: no numpy"; exit 0; }
make -s build/maktabasauti_cli >/dev/null
HAVE_FAAD=0
if python3 -c 'import ctypes.util,sys; sys.exit(0 if ctypes.util.find_library("faad") else 1)'; then cc -O2 -o build/sauti_faad_raw_oracle tools/sauti_faad_raw_oracle.c -ldl && HAVE_FAAD=1; else echo "NOTE: no libfaad: FAAD2 comparison skipped (FFmpeg only)"; fi
W="$(mktemp -d "${TMPDIR:-/tmp}/sauti-main.XXXXXX")"; trap 'rm -rf -- "$W"' EXIT
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
files="main_st.m4a main_st.aac main_st_crc.aac main_st.loas main_c3_long.m4a main_c4.m4a main_c5.m4a main_c6.m4a main_c6_pns.m4a main_c7.m4a main960_mono.m4a main960_st.m4a main960_st.loas main_mono.m4a main_long.m4a main_var.m4a main_pns.m4a main_pnsst.m4a $(cd tests/data && ls main_r[01][0-9].m4a | grep -v main_r12)"
for f in $files; do
  ch=2; case "$f" in main_mono*|main_long*|main_pns.m4a) ch=1;; main960_mono*) ch=1;; main_c3*) ch=3;; main_c4*) ch=4;; main_c5*) ch=5;; main_c6*) ch=6;; main_c7*) ch=8;; esac
  build/maktabasauti_cli "tests/data/$f" "$W/s.sau" | sed "s/^/$f: /"
  case "$f" in
    main_var.m4a|main_pns.m4a|main_pnsst.m4a) echo "$f vs FFmpeg: not compared (stale prediction_used flags / no PNS predictor reset in FFmpeg)" ;;
    *) ffmpeg -v error -y -i "tests/data/$f" -f f32le "$W/s.ff"; r="$(cmp "$W/s.sau" "$W/s.ff" "$ch" "$MIN" 0)"; echo "$f vs FFmpeg: $r"; case "$r" in OK*) ;; *) fail=1;; esac ;;
  esac
  if [ "$HAVE_FAAD" = 1 ]; then
    case "$f" in
      *.aac|*.loas|main_c[3-7]*|main960*|main_r06*|main_r07*|main_r08*|main_r09*|main_r10*|main_r11*) ;;
      *) python3 -I tools/mp4_dump_aus.py "tests/data/$f" "$W/s" >/dev/null
         build/sauti_faad_raw_oracle "$W/s.asc" "$W/s.aus" "$W/s.faad" >/dev/null 2>&1
         mn="$FMIN"; case "$f" in main_pns*) mn=100;; esac
         r="$(cmp "$W/s.sau" "$W/s.faad" "$ch" "$mn" 2500)"; echo "$f vs FAAD2:  $r"; case "$r" in OK*) ;; *) fail=1;; esac ;;
    esac
  fi
done
exit $fail
