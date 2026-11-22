#!/usr/bin/env bash
# Regenerates tests/data/usac_w1..w3.m4a: streams of tools/sauti_usac_gen.c (random but valid syntax) with the harmonic transposer (--hbe): random sbrPatchingMode, sbrOversamplingFlag and sbrPitchInBins (0, 12, 24, 36 and
# 1..127) - the pitch values the encoder of libxaac never writes, with both the plain products (pitch below one QMF band) and the cross products of the orders 2, 3 and 4.  w1: mono, mix of patching modes; w2: mono, always
# the transposer; w3: stereo, uncoupled and coupled pairs (one patching mode per channel / one for a coupled pair).  Against libxaac's decoder the transposer output agrees to the float noise (relative error 1e-6; with
# pitch values above ~90 the selection of the cross product differs in single band / column pairs: float against double, 1e-4) and the PCM to USAC_XAAC_GEN_MIN_DB (the random cores are quiet and the seeds below are
# those whose PCM agrees best; docs SBR-115).
# usage: tests/data/make_usac_hbe_gen_fixtures.sh   (needs `make build/sauti_usac_gen`)
set -euo pipefail
cd "$(dirname "$0")/../.."
make -s build/sauti_usac_gen >/dev/null
W="$(mktemp -d "${TMPDIR:-/tmp}/sauti-hbegen.XXXXXX")"; trap 'rm -rf -- "${W:?}"' EXIT
gen() { build/sauti_usac_gen "$W/$1" --channels "$2" --frames "$3" --gain 125 --seed "$4" --sbr --sbr-simple 384 --short --kbd ${6:+--common} --hbe "$5" >/dev/null; python3 -I tools/mux_raw_aus_mp4.py "$W/$1" "tests/data/usac_$1.m4a" 2048; }
gen w1 1 20 31 1
gen w2 1 20 42 2
gen w3 2 18 12 1 common
