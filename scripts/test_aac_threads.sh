#!/usr/bin/env bash
# Re-entrancy of the native AAC decoders: the committed synthetic streams (tests/data: LC stereo and 5.1, HE-AAC v1 and v2, M4A, LOAS, ADIF) decoded at the same time by
# several threads must give bit-identical PCM (also after a seek).  No encoders and no user data are needed: the fixtures are in the repository, so there is nothing to SKIP.
# usage: scripts/test_aac_threads.sh [THREADS=6] [ROUNDS=2]
# REQUIRE_TSAN=1 (set in CI): the test is built with ThreadSanitizer and FAILS when the runtime is missing, instead of falling back.  Locally without libtsan it falls back
# to AddressSanitizer + the bit-identical comparison and says so (data races without corruption are then NOT ruled out).
set -euo pipefail
cd "$(dirname "$0")/.."
T="${1:-6}"; R="${2:-2}"
D=tests/data
for f in lc.aac he1.aac he2.aac he1.m4a he1.loas he1.adif m51.aac; do [ -r "$D/$f" ] || { echo "FAIL: missing fixture $D/$f"; exit 1; }; done
if make -s build/test_aac_threads_tsan >/dev/null 2>&1; then BIN=build/test_aac_threads_tsan; echo "sanitizer: ThreadSanitizer"
elif [ "${REQUIRE_TSAN:-0}" = 1 ]; then echo "FAIL: REQUIRE_TSAN=1 but the ThreadSanitizer test could not be built (is libtsan installed?)"; exit 1
else make -s build/test_aac_threads_asan >/dev/null; BIN=build/test_aac_threads_asan; echo "NOTE: no ThreadSanitizer runtime here (package libtsan): AddressSanitizer + bit-identical PCM comparison only; data races without corruption are NOT ruled out"; fi
TSAN_OPTIONS="halt_on_error=1 exitcode=66" "$BIN" "$T" "$R" "$D/lc.aac" "$D/he1.aac" "$D/he2.aac" "$D/he1.m4a" "$D/he1.loas" "$D/he1.adif" "$D/m51.aac"
