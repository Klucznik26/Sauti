#!/usr/bin/env bash
# Decodes REAL WinRec .dvs files (a directory kept OUTSIDE the repository, untrusted data; e.g. ~/Sauti-tools/real-files/dvs/real) with the production decoder and compares every sample with the independent Python reference
# (tests/compare_dvsm_reference.py).  A file that is refused by the decoder or differs in a sample is a failure.  No file = SKIP (exit 0).  usage: scripts/test_real_dvs.sh DIR
set -euo pipefail
cd "$(dirname "$0")/.."
D="${1:?usage: test_real_dvs.sh DIR}"
shopt -s nullglob nocaseglob
files=("$D"/*.dvs)
[ ${#files[@]} -gt 0 ] || { echo "SKIP: no .dvs files in $D"; exit 0; }
make -s build/dump_decoder_dvsm >/dev/null
W="$(mktemp -d "${TMPDIR:-/tmp}/sauti-realdvs.XXXXXX")"; trap 'rm -rf -- "${W:?}"' EXIT
fail=0; n=0
for f in "${files[@]}"; do
  n=$((n+1))
  if ./build/dump_decoder_dvsm "$f" "$W/o.raw" >/dev/null 2>&1 && python3 -I tests/compare_dvsm_reference.py "$f" "$W/o.raw" 2>&1 | grep -q "^PASS"; then :; else echo "FAIL $(basename "$f")"; fail=1; fi
done
echo "$n real .dvs files: $([ $fail = 0 ] && echo 'all bit-identical to the independent reference' || echo 'FAILURES')"
exit $fail
