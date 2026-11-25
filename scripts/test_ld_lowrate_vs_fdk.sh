#!/usr/bin/env bash
# AAC-LD / AAC-ELD with a core rate of 16 kHz (tests/data/ld_lowrate/, FDK encoder streams: .m4a, plus the raw access units .aus and the config .asc for the oracle).  The standard has
# no band tables below 24 kHz for the 512 / 480 layouts; FDK (and now Sauti) use the 24 kHz tables for 24 kHz and below.  Checks: the file decodes, the length is the expected one,
# and, when the 32-bit-PCM FDK oracle can be built (tools/sauti_fdk_raw_oracle.c; SAUTI_FDK32, default ~/Sauti-tools/fdk32), the SNR against FDK is at least LDLOW_FDK_MIN_DB (default 60;
# measured 72-90 dB).  Without the oracle only the length and the RMS (0.1761 +- 1 %, the encoder input) are checked.  usage: scripts/test_ld_lowrate_vs_fdk.sh (needs python3 + numpy)
set -euo pipefail
cd "$(dirname "$0")/.."
MIN="${LDLOW_FDK_MIN_DB:-60}"; FDK="${SAUTI_FDK32:-$HOME/Sauti-tools/fdk32}"
python3 -c 'import numpy' 2>/dev/null || { echo "SKIP: no numpy"; exit 0; }
make -s build/maktabasauti_cli >/dev/null
HAVE=0
if [ -f "$FDK/build/libfdk-aac.so" ]; then
  cc -O2 -I"$FDK/libAACdec/include" -I"$FDK/libSYS/include" -o build/sauti_fdk_raw_oracle tools/sauti_fdk_raw_oracle.c -L"$FDK/build" -lfdk-aac -Wl,-rpath,"$FDK/build" && HAVE=1
else echo "NOTE: no 32-bit PCM libfdk-aac in $FDK/build: FDK comparison skipped"; fi
W="$(mktemp -d "${TMPDIR:-/tmp}/sauti-ldlow.XXXXXX")"; trap 'rm -rf -- "$W"' EXIT
fail=0
for spec in ld_512_16000_1:48640:1 ld_480_16000_2:96960:2 eld_512_16000_1_r0:48640:1; do
  IFS=: read -r n len ch <<<"$spec"; D=tests/data/ld_lowrate/$n
  build/maktabasauti_cli "$D.m4a" "$W/$n.ours" >/dev/null
  [ "$HAVE" = 1 ] && build/sauti_fdk_raw_oracle "$D.asc" "$D.aus" "$W/$n.fdk" >/dev/null
  python3 -I - "$W/$n" "$len" "$ch" "$MIN" "$HAVE" <<'PY' || fail=1
import sys, numpy as np
p, n, ch, mn, have = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), float(sys.argv[4]), sys.argv[5] == '1'
a = np.fromfile(p + '.ours', '<f4'); ok = len(a) == n
rms = float(np.sqrt((a.astype(float) ** 2).mean())); ok = ok and abs(rms - 0.1761) < 0.002; msg = "length %d rms %.4f" % (len(a), rms)
if have:
    b = np.fromfile(p + '.fdk', '<f4'); ok = ok and len(a) == len(b)
    if len(a) == len(b):
        A = a.reshape(-1, ch).astype(float); B = b.reshape(-1, ch).astype(float)
        s = min(10 * np.log10((B[:, c] ** 2).sum() / max(((A[:, c] - B[:, c]) ** 2).sum(), 1e-30)) for c in range(ch)); ok = ok and s >= mn; msg += ", vs FDK %.1f dB" % s
print(("OK   " if ok else "FAIL ") + p.rsplit('/', 1)[1] + ": " + msg); sys.exit(0 if ok else 1)
PY
done
exit $fail
