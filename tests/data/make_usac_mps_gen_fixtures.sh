#!/usr/bin/env bash
# Regenerates tests/data/usac_y1..y3.m4a: streams of tools/sauti_usac_gen.c (random but valid syntax) with MPEG Surround 212, stereoConfigIndex 2 and a REAL residual (the second core channel carries random spectra): y1 bsResidualBands 8 /
# decorrelator configuration 0, y2: 20 / 2, y3: 14 / 1; eSBR 2:1 (--sbr-simple 384), 24 frames.  The libxaac encoder cannot make such streams; the oracle is the decoder of libxaac, compared at the level of the upmix
# (tests/test_usac_mps.c: golden files from an instrumented copy) because random eSBR data make the PCM of two independent decoders differ by far more than the upmix does.
# usac_y4..y6: stereoConfigIndex 1 (one core channel, rotation upmix): y4 without phase coding, decorrelator 0; y5 with phase coding (IPD / OPD, one IPD set of 10 bands per frame), OPD smoothing 0, decorrelator 0; y6 phase coding with OPD smoothing 1, decorrelator 2.
# usac_y7..y9: stereoConfigIndex 2 (prediction upmix) with phase coding: y7 without residual bands, OPD smoothing 0; y8 12 residual bands, OPD smoothing 1, decorrelator 1, with the anti-phase special case (bands 0..3: CLD 0, ICC 0, IPD 8); y9 3 residual bands, decorrelator 2, special case.
# usac_y10, y11: temporal shaping STP (bsTempShapeConfig 1; bsTempShapeEnable and the channel flags random per frame): y10 stereoConfigIndex 1, decorrelator 0; y11 stereoConfigIndex 2 with 8 residual bands, decorrelator 1.
# usage: tests/data/make_usac_mps_gen_fixtures.sh
set -euo pipefail
cd "$(dirname "$0")/../.."
make -s build/sauti_usac_gen >/dev/null
W="$(mktemp -d "${TMPDIR:-/tmp}/sauti-mpsgen.XXXXXX")"; trap 'rm -rf -- "${W:?}"' EXIT
gen() { build/sauti_usac_gen "$W/$1" --channels 2 --frames 24 --gain 110 --seed "$2" --sbr --sbr-simple 384 --common --rate 3 --mps "${5:-2}" --mps-res "$3" --mps-decorr "$4" ${6:-} >/dev/null; python3 -I tools/mux_raw_aus_mp4.py "$W/$1" "tests/data/usac_$1.m4a" 2048; }
gen y1 5 8 0
gen y2 9 20 2
gen y3 8 14 1
gen y4 21 0 0 1
gen y5 22 0 0 1 "--mps-phase 1 --mps-opd 0"
gen y6 25 0 2 1 "--mps-phase 1 --mps-opd 1"
gen y7 41 0 0 2 "--mps-phase 1 --mps-opd 0"
gen y8 42 12 1 2 "--mps-phase 1 --mps-opd 1 --mps-anti"
gen y9 43 3 2 2 "--mps-phase 1 --mps-opd 0 --mps-anti"
gen y10 71 0 0 1 "--mps-stp 1"
gen y11 72 8 1 2 "--mps-stp 1"
