#!/usr/bin/env bash
# USAC (xHE-AAC) frequency-domain streams (tests/data/usac_*.m4a, tools/sauti_usac_gen.c) against two independent decoders.
#   FFmpeg (float; 125-139 dB, 100+ dB with eSBR [USAC_SBR_MIN_DB]; the first frame of every stream has the sine window: FDK takes the first frame's own window shape as the previous one, FFmpeg sine) on the streams whose tools it implements the way the standard does: it seeds the noise generator of the first channel with 0 (FDK and the standard: 0x3039), skips noise
#   filling at noise_level 0 (the generator still advances) and never applies the TNS of independent windows, so usac_m3 and usac_s2 are not compared with it.
#   FDK AAC 32-bit (tools/sauti_fdk_raw_oracle.c, see its header; limiter off): ~90 dB at best (its noise floor is about -143 dB RMS), every stream.  Known FDK 2.0.3 defect: with tns_active = 1, tns_on_lr = 0 and M/S it forgets to update the spectrum exponent after the M/S
#   (left x0.5, right scaled by its old exponent; docs/AAC_FAMILY_ASM.md SBR-105), the fixtures avoid that combination for FDK.
#   eSBR streams (usac_e1..e4, 2:1, core 24 kHz): FFmpeg ~120-134 dB on e1 and e2 (classic SBR; e1 and e2 avoid what FFmpeg gets wrong); FDK only reaches 35-80 dB there, because the SBR chain
#   (shared with HE-AAC) differs from FDK's at the 1-2 % level of the HF part (open, see docs/AAC_FAMILY_ASM.md), so for FDK the total SNR bound is USAC_SBR_FDK_MIN_DB (30) and the energy of the 12-24 kHz bands
#   must agree (median < 1 dB): that is what a mis-parsed envelope, grid or header breaks.
# bounds: USAC_MIN_DB (FFmpeg, default 120), USAC_FDK_MIN_DB (default 80).  SKIP (exit 0, stated loudly) without ffmpeg / numpy / the FDK oracle.
# usage: scripts/test_usac_vs_ffmpeg.sh
set -euo pipefail
cd "$(dirname "$0")/.."
MIN="${USAC_MIN_DB:-120}"; FMIN="${USAC_FDK_MIN_DB:-80}"; SMIN="${USAC_SBR_FDK_MIN_DB:-30}"
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
bands() {  # bands OURS.f32 THEIRS.f32 CHANNELS: the energy of 1.5 kHz bands between 12 and 24 kHz (the eSBR region) must agree: median < 1 dB, 90% < 8 dB (a mis-parsed envelope shows up here at once)
  python3 -I - "$@" <<'PY'
import sys, numpy as np
a = np.fromfile(sys.argv[1], dtype='<f4'); b = np.fromfile(sys.argv[2], dtype='<f4'); ch = int(sys.argv[3]); fl = 2048
n = min(len(a), len(b)) // ch * ch; a = a[:n].reshape(-1, ch); b = b[:n].reshape(-1, ch); win = np.hanning(fl); dev = []
edges = list(range(12000, 24001, 1500))
for f in range(2, len(a) // fl):
    for c in range(ch):
        x = np.abs(np.fft.rfft(a[f*fl:(f+1)*fl, c] * win)) ** 2; y = np.abs(np.fft.rfft(b[f*fl:(f+1)*fl, c] * win)) ** 2
        for lo, hi in zip(edges[:-1], edges[1:]):
            i0 = int(lo / 24000 * fl / 2); i1 = int(hi / 24000 * fl / 2); ex = x[i0:i1].sum(); ey = y[i0:i1].sum()
            if ex > 1e-9 and ey > 1e-9: dev.append(abs(10 * np.log10(ex / ey)))
d = np.array(dev)
print("%s median %.2f dB, 90%% %.2f dB, max %.2f dB (%d bands)" % ("OK" if len(d) and np.median(d) < 1.0 and np.percentile(d, 90) < 8.0 else "FAIL", np.median(d), np.percentile(d, 90), d.max(), len(d)))
PY
}
fail=0
for f in m1 m2 m3 m4 m5 s1 s2 s3 s4 c1 c2 c3 c4 c5 e1 e2 e3 e4 e5 e6 e7; do
  ch=1; case "$f" in s*|c*|e2|e4|e7) ch=2;; esac
  python3 -I tools/mp4_dump_aus.py "tests/data/usac_$f.m4a" "$W/$f" >/dev/null
  build/sauti_usac_raw "$W/$f" "$W/$f.sau" 2>/dev/null
  case "$f" in
    m3|m5|s2|s4) echo "usac_$f vs FFmpeg: not compared (FFmpeg's noise filling / TNS of independent windows differ from the standard)" ;;
    e3|e4|e6|e7) echo "usac_$f vs FFmpeg: not compared (explicit headers: FFmpeg reads two stray bits; it also ignores SbrInfo xover changes and mishandles some sinusoids)" ;;
    c1|c2|c3|c4|c5) echo "usac_$f vs FFmpeg: not compared (complex prediction: FFmpeg builds the downmix of the predicted bands the wrong way round and has no previous-frame MDST filter)" ;;
    *) ffmpeg -v error -y -i "tests/data/usac_$f.m4a" -f f32le "$W/$f.ff"; fm="$MIN"; case "$f" in e*) fm="${USAC_SBR_MIN_DB:-95}";; esac; r="$(cmp "$W/$f.sau" "$W/$f.ff" "$ch" "$fm")"; echo "usac_$f vs FFmpeg: $r"; case "$r" in OK*) ;; *) fail=1;; esac ;;
  esac
  if [ -x build/sauti_fdk_raw_oracle ]; then
    build/sauti_fdk_raw_oracle "$W/$f.asc" "$W/$f.aus" "$W/$f.fdk" >/dev/null 2>&1
    case "$f" in
      e*) r="$(cmp "$W/$f.sau" "$W/$f.fdk" "$ch" "$SMIN")"; echo "usac_$f vs FDK:    $r"; case "$r" in OK*) ;; *) fail=1;; esac
          r="$(bands "$W/$f.sau" "$W/$f.fdk" "$ch")"; echo "usac_$f HF bands:  $r"; case "$r" in OK*) ;; *) fail=1;; esac ;;
      *) r="$(cmp "$W/$f.sau" "$W/$f.fdk" "$ch" "$FMIN")"; echo "usac_$f vs FDK:    $r"; case "$r" in OK*) ;; *) fail=1;; esac ;;
    esac
  else echo "NOTE: no build/sauti_fdk_raw_oracle: FDK comparison skipped"; fi
done
exit $fail
