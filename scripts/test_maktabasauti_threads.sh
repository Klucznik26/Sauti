#!/usr/bin/env bash
# MaktabaSauti: niezaleznosc instancji pod wieloma watkami (nowe API, wlasny alokator na watek, wspolne rejestry tylko do odczytu).  Fixtury w tests/data: nic do pominiecia.
# REQUIRE_TSAN=1 (w CI): buduje pod ThreadSanitizerem i konczy sie BLEDEM, gdy runtime TSan jest niedostepny; lokalnie bez libtsan: ASan/UBSan + porownanie bit w bit z komunikatem.
# usage: scripts/test_maktabasauti_threads.sh [THREADS=8] [ROUNDS=2]
set -euo pipefail
cd "$(dirname "$0")/.."
T="${1:-8}"; R="${2:-2}"; D=tests/data
FILES="$D/lc.aac $D/he1.aac $D/he2.aac $D/he1.m4a $D/he1.loas $D/he1.adif $D/m51.aac"
PROBE_FILES="$(ls $D/probe_real.* 2>/dev/null || true)"   # wiele formatow: rownolegle rozpoznawanie (nie dekoduja sie wszystkie: tylko AAC z FILES tworzy wzorzec dekodowania)
for f in $FILES; do [ -r "$f" ] || { echo "FAIL: missing fixture $f"; exit 1; }; done
if make -s build/test_maktabasauti_threads_tsan >/dev/null 2>&1; then BIN=build/test_maktabasauti_threads_tsan; echo "sanitizer: ThreadSanitizer"
elif [ "${REQUIRE_TSAN:-0}" = 1 ]; then echo "FAIL: REQUIRE_TSAN=1 but the ThreadSanitizer build failed (is libtsan installed?)"; exit 1
else make -s build/test_maktabasauti_threads_asan >/dev/null; BIN=build/test_maktabasauti_threads_asan; echo "NOTE: no ThreadSanitizer runtime here (libtsan): ASan/UBSan + bit-identical comparison only; data races without corruption are NOT ruled out"; fi
# shellcheck disable=SC2086
TSAN_OPTIONS="halt_on_error=1 exitcode=66" "$BIN" "$T" "$R" $FILES -- $PROBE_FILES
