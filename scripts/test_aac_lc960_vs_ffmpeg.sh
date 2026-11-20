#!/usr/bin/env bash
# AAC-LC with 960-sample frames (tests/data/lc960_*.{m4a,loas}) against two independent decoders: FFmpeg (float arithmetic like ours: the decoders agree to float rounding, ~138 dB; bound
# LC960_MIN_DB, default 130, lag 0, same length) and, when the 32-bit-PCM FDK AAC build is present (tools/sauti_fdk_raw_oracle.c explains it; SAUTI_FDK32 = its source tree, default
# ~/Sauti-tools/fdk32), FDK AAC (fixed point: ~90 dB; bound LC960_FDK_MIN_DB, default 80; FDK does not trim the codec delay, so the best lag within +-2500 samples is searched).
# Perceptual noise substitution (lc960_pns*.m4a) needs another yardstick: the standard does not fix the random sequence, so FDK (and FAAD2) draw other noise than FFmpeg / Sauti, which share one
# generator (lc960_pns.m4a agrees with FFmpeg to ~137 dB).  With M/S the standard makes the noise of both channels correlated where both are noise and ms_used is set (lc960_pnsall.m4a: FDK, FAAD2 and
# Sauti correlate L/R at 0.42-0.45, FFmpeg at -0.01 and is NOT a reference there).  For these files FFmpeg is compared only on lc960_pns.m4a, and FDK by statistics: L/R correlation within 0.05 and the
# RMS of each channel within 10 % (TNS filters the noise, so its energy depends on the noise realisation).
# usage: scripts/test_aac_lc960_vs_ffmpeg.sh   (needs ffmpeg and python3 with numpy; SKIP, stated loudly, exit 0 without them: that is not a proof of agreement)
set -euo pipefail
cd "$(dirname "$0")/.."
MIN="${LC960_MIN_DB:-130}"; FMIN="${LC960_FDK_MIN_DB:-80}"; FDK="${SAUTI_FDK32:-$HOME/Sauti-tools/fdk32}"
command -v ffmpeg >/dev/null || { echo "SKIP: no ffmpeg"; exit 0; }
python3 -c 'import numpy' 2>/dev/null || { echo "SKIP: no numpy"; exit 0; }
make -s build/maktabasauti_cli >/dev/null
HAVE_FDK=0
if [ -f "$FDK/build/libfdk-aac.so" ]; then
  cc -O2 -I"$FDK/libAACdec/include" -I"$FDK/libSYS/include" -o build/sauti_fdk_raw_oracle tools/sauti_fdk_raw_oracle.c -L"$FDK/build" -lfdk-aac -Wl,-rpath,"$FDK/build" && HAVE_FDK=1
else echo "NOTE: no 32-bit PCM libfdk-aac in $FDK/build: FDK comparison skipped (FFmpeg only)"; fi
W="$(mktemp -d "${TMPDIR:-/tmp}/sauti-lc960.XXXXXX")"; trap 'rm -rf -- "$W"' EXIT
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
stat() {  # stat OURS.f32 THEIRS.f32  -> compares L/R correlation and per-channel RMS of two stereo decodes (delay-independent)
  python3 -I - "$@" <<'PY'
import sys, numpy as np
def feat(p):
    a = np.fromfile(p, dtype='<f4').reshape(-1, 2)[4000:-4000].astype(float); l, r = a[:, 0], a[:, 1]
    return float(np.dot(l, r) / np.sqrt(np.dot(l, l) * np.dot(r, r))), float(l.std()), float(r.std())
a = feat(sys.argv[1]); b = feat(sys.argv[2])
ok = abs(a[0] - b[0]) < 0.05 and abs(a[1] / b[1] - 1) < 0.10 and abs(a[2] / b[2] - 1) < 0.10
print("%s corr %.4f vs %.4f, rms L %.4g vs %.4g, rms R %.4g vs %.4g" % ("OK" if ok else "FAIL", a[0], b[0], a[1], b[1], a[2], b[2]))
PY
}
fail=0
for f in lc960_st.m4a lc960_mono.m4a lc960_st.loas lc960_pns.m4a lc960_pnsms.m4a lc960_pnsall.m4a $(cd tests/data && ls lc960_r[0-9][0-9].m4a); do
  ch=2; case "$f" in *mono*) ch=1;; esac
  build/maktabasauti_cli "tests/data/$f" "$W/s.sau" | sed "s/^/$f: /"
  ffmpeg -v error -y -i "tests/data/$f" -f f32le "$W/s.ff"
  case "$f" in
    lc960_pnsms.m4a|lc960_pnsall.m4a) echo "$f vs FFmpeg: not compared (FFmpeg has no correlated noise)" ;;
    *) r="$(cmp "$W/s.sau" "$W/s.ff" "$ch" "$MIN" 0)"; echo "$f vs FFmpeg: $r"; case "$r" in OK*) ;; *) fail=1;; esac ;;
  esac
  if [ "$HAVE_FDK" = 1 ]; then
    case "$f" in
      *.loas) build/sauti_fdk_raw_oracle --loas "tests/data/$f" "$W/s.fdk" 2>/dev/null ;;
      *) python3 -I tools/mp4_dump_aus.py "tests/data/$f" "$W/s" >/dev/null && build/sauti_fdk_raw_oracle "$W/s.asc" "$W/s.aus" "$W/s.fdk" 2>/dev/null ;;
    esac
    case "$f" in
      lc960_pns*.m4a) r="$(stat "$W/s.sau" "$W/s.fdk")"; echo "$f vs FDK (statistics): $r" ;;
      *) r="$(cmp "$W/s.sau" "$W/s.fdk" "$ch" "$FMIN" 2500)"; echo "$f vs FDK:    $r" ;;
    esac
    case "$r" in OK*) ;; *) fail=1;; esac
  fi
done
exit $fail
