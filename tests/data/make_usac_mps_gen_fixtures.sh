#!/usr/bin/env bash
# Regenerates tests/data/usac_y1..y3.m4a: streams of tools/sauti_usac_gen.c (random but valid syntax) with MPEG Surround 212, stereoConfigIndex 2 and a REAL residual (the second core channel carries random spectra): y1 bsResidualBands 8 /
# decorrelator configuration 0, y2: 20 / 2, y3: 14 / 1; eSBR 2:1 (--sbr-simple 384), 24 frames.  The libxaac encoder cannot make such streams; the oracle is the decoder of libxaac, compared at the level of the upmix
# (tests/test_usac_mps.c: golden files from an instrumented copy) because random eSBR data make the PCM of two independent decoders differ by far more than the upmix does.
# usage: tests/data/make_usac_mps_gen_fixtures.sh
set -euo pipefail
cd "$(dirname "$0")/../.."
make -s build/sauti_usac_gen >/dev/null
W="$(mktemp -d "${TMPDIR:-/tmp}/sauti-mpsgen.XXXXXX")"; trap 'rm -rf -- "${W:?}"' EXIT
gen() { build/sauti_usac_gen "$W/$1" --channels 2 --frames 24 --gain 110 --seed "$2" --sbr --sbr-simple 384 --common --rate 3 --mps 2 --mps-res "$3" --mps-decorr "$4" >/dev/null; python3 -I tools/mux_raw_aus_mp4.py "$W/$1" "tests/data/usac_$1.m4a" 2048; }
gen y1 5 8 0
gen y2 9 20 2
gen y3 8 14 1
