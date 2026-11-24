#!/usr/bin/env bash
# AAC-LD / AAC-ELD against FFmpeg: decodes tests/data/{ld,eld}_{st,mono}.m4a with the MaktabaSauti registry (tools/maktabasauti_cli) and with FFmpeg, aligns them (FFmpeg also applies the
# gapless trim, lag 0) and requires SNR >= MIN dB (default 130; the decoders agree to float rounding, ~139 dB).
# usage: scripts/test_aac_er_vs_ffmpeg.sh   (needs ffmpeg and python3 with numpy; SKIP, stated loudly, exit 0 without them: that is not a proof of agreement)
set -euo pipefail
cd "$(dirname "$0")/.."
MIN="${ER_MIN_DB:-130}"
command -v ffmpeg >/dev/null || { echo "SKIP: no ffmpeg"; exit 0; }
python3 -c 'import numpy' 2>/dev/null || { echo "SKIP: no numpy"; exit 0; }
make -s build/maktabasauti_cli >/dev/null
W="$(mktemp -d "${TMPDIR:-/tmp}/sauti-er.XXXXXX")"; trap 'rm -rf -- "$W"' EXIT
fail=0
for f in ld_st eld_st ld_mono eld_mono ld_hq eld_hq; do
  ch=2; case "$f" in *mono*) ch=1;; esac
  build/maktabasauti_cli "tests/data/$f.m4a" "$W/$f.sau" | sed "s/^/$f: /"
  ffmpeg -v error -y -i "tests/data/$f.m4a" -f f32le "$W/$f.ff"
  r="$(python3 -I - "$W/$f.sau" "$W/$f.ff" "$ch" "$MIN" <<'PY'
import sys, numpy as np
a = np.fromfile(sys.argv[1], dtype='<f4'); b = np.fromfile(sys.argv[2], dtype='<f4'); ch = int(sys.argv[3]); mn = float(sys.argv[4])
n = min(len(a), len(b)) // ch * ch; a = a[:n].reshape(-1, ch); b = b[:n].reshape(-1, ch)
x = a[4000:n // ch - 4000]; y = b[4000:n // ch - 4000]
e = float(np.sum((x - y) ** 2)); s = float(np.sum(y ** 2)); snr = 999.0 if e == 0 else 10 * np.log10(s / e)
xh = a[:4000]; yh = b[:4000]; eh = float(np.sum((xh - yh) ** 2)); sh = float(np.sum(yh ** 2)); head = 999.0 if eh == 0 else 10 * np.log10(sh / eh)   # the start too: the window shape of the first frames (a wrong left half gave 12 - 17 dB here for months)
print("%s %.2f dB over %d frames (lengths %d / %d), first 4000 frames %.2f dB" % ("OK" if snr >= mn and head >= 60 and len(a) == len(b) else "FAIL", snr, len(x), len(a), len(b), head))
PY
)"
  echo "$f: $r"; case "$r" in OK*) ;; *) fail=1;; esac
done
exit $fail
