#!/usr/bin/env bash
# MaktabaSauti: pierwsze uzycie kazdego kodeka z wielu watkow naraz w swiezych procesach (wyscigi inicjalizacji leniwej; patrz tests/test_maktabasauti_coldstart.c).
# REQUIRE_TSAN=1 (w CI): buduje pod ThreadSanitizerem i konczy sie BLEDEM, gdy runtime TSan jest niedostepny; lokalnie bez libtsan: ASan/UBSan + porownanie wynikow z komunikatem.
# usage: scripts/test_maktabasauti_coldstart.sh [THREADS=8] [PROCESSES=12]
set -euo pipefail
cd "$(dirname "$0")/.."
T="${1:-8}"; P="${2:-12}"; D=tests/data
FILES="$D/lc.aac $D/lc.loas $D/lc.adif $D/lc.m4a $D/lc_frag.m4a $D/lc_mono.aac $D/he1_mono.aac $D/he1.aac $D/he2.aac $D/he1.m4a $D/he1.loas $D/he1.adif $D/m51.aac $(ls $D/probe_real.*)"
# shellcheck disable=SC2086
for f in $FILES; do [ -r "$f" ] || { echo "FAIL: missing fixture $f"; exit 1; }; done
if make -s build/test_maktabasauti_coldstart_tsan >/dev/null 2>&1; then BIN=build/test_maktabasauti_coldstart_tsan; echo "sanitizer: ThreadSanitizer"
elif [ "${REQUIRE_TSAN:-0}" = 1 ]; then echo "FAIL: REQUIRE_TSAN=1 but the ThreadSanitizer build failed (is libtsan installed?)"; exit 1
else make -s build/test_maktabasauti_coldstart_asan >/dev/null; BIN=build/test_maktabasauti_coldstart_asan; echo "NOTE: no ThreadSanitizer runtime here (libtsan): ASan/UBSan + comparison with the single-thread result only; data races without corruption are NOT ruled out"; fi
# shellcheck disable=SC2086
TSAN_OPTIONS="halt_on_error=1 exitcode=66" ASAN_OPTIONS=allocator_may_return_null=1 "$BIN" "$T" "$P" $FILES
