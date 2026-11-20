#!/usr/bin/env bash
# AAC-LD / AAC-ELD in LOAS/LATM (and the M4A fixtures) against the Fraunhofer FDK AAC decoder built with 32-bit PCM (tools/sauti_fdk_raw_oracle.c explains the build; set SAUTI_FDK32 to the
# source tree that holds build/libfdk-aac.so, default ~/Sauti-tools/fdk32).  FDK is fixed-point, so the two decoders agree to ~90 dB (FFmpeg, which has float arithmetic like ours, agrees to
# ~139 dB: scripts/test_aac_er_vs_ffmpeg.sh); the bound here is MIN dB (default 80), far above what any real defect leaves.
# usage: scripts/test_aac_er_vs_fdk.sh   (needs the FDK build and python3 with numpy; SKIP, stated loudly, exit 0 without them: that is not a proof of agreement)
set -euo pipefail
cd "$(dirname "$0")/.."
FDK="${SAUTI_FDK32:-$HOME/Sauti-tools/fdk32}"; MIN="${ER_FDK_MIN_DB:-80}"
[ -f "$FDK/build/libfdk-aac.so" ] || { echo "SKIP: no 32-bit PCM libfdk-aac in $FDK/build (see tools/sauti_fdk_raw_oracle.c)"; exit 0; }
python3 -c 'import numpy' 2>/dev/null || { echo "SKIP: no numpy"; exit 0; }
make -s build/maktabasauti_cli >/dev/null
cc -O2 -I"$FDK/libAACdec/include" -I"$FDK/libSYS/include" -o build/sauti_fdk_raw_oracle tools/sauti_fdk_raw_oracle.c -L"$FDK/build" -lfdk-aac -Wl,-rpath,"$FDK/build"
W="$(mktemp -d "${TMPDIR:-/tmp}/sauti-erfdk.XXXXXX")"; trap 'rm -rf -- "$W"' EXIT
fail=0
for f in ld_st.loas eld_st.loas; do
  build/maktabasauti_cli "tests/data/$f" "$W/s.sau" | sed "s/^/$f: /"
  build/sauti_fdk_raw_oracle --loas "tests/data/$f" "$W/s.fdk" 2>/dev/null
  r="$(python3 -I - "$W/s.sau" "$W/s.fdk" "$MIN" <<'PY'
import sys, numpy as np
a = np.fromfile(sys.argv[1], dtype='<f4').reshape(-1, 2); b = np.fromfile(sys.argv[2], dtype='<f4').reshape(-1, 2); mn = float(sys.argv[3])
n = min(len(a), len(b)); x = a[4000:n - 4000]; y = b[4000:n - 4000]
e = float(np.sum((x - y) ** 2)); s = float(np.sum(y ** 2)); snr = 999.0 if e == 0 else 10 * np.log10(s / e)
print("%s %.2f dB over %d frames (lengths %d / %d)" % ("OK" if snr >= mn else "FAIL", snr, len(x), len(a), len(b)))
PY
)"
  echo "$f: $r"; case "$r" in OK*) ;; *) fail=1;; esac
done
exit $fail
