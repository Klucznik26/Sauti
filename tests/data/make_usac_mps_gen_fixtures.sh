#!/usr/bin/env bash
# Regenerates tests/data/usac_y1..y3.m4a: streams of tools/sauti_usac_gen.c (random but valid syntax) with MPEG Surround 212, stereoConfigIndex 2 and a REAL residual (the second core channel carries random spectra): y1 bsResidualBands 8 /
# decorrelator configuration 0, y2: 20 / 2, y3: 14 / 1; eSBR 2:1 (--sbr-simple 384), 24 frames.  The libxaac encoder cannot make such streams; the oracle is the decoder of libxaac, compared at the level of the upmix
# (tests/test_usac_mps.c: golden files from an instrumented copy) because random eSBR data make the PCM of two independent decoders differ by far more than the upmix does.
# usac_y4..y6: stereoConfigIndex 1 (one core channel, rotation upmix): y4 without phase coding, decorrelator 0; y5 with phase coding (IPD / OPD, one IPD set of 10 bands per frame), OPD smoothing 0, decorrelator 0; y6 phase coding with OPD smoothing 1, decorrelator 2.
# usac_y7..y9: stereoConfigIndex 2 (prediction upmix) with phase coding: y7 without residual bands, OPD smoothing 0; y8 12 residual bands, OPD smoothing 1, decorrelator 1, with the anti-phase special case (bands 0..3: CLD 0, ICC 0, IPD 8); y9 3 residual bands, decorrelator 2, special case.
# usac_y10, y11: temporal shaping STP (bsTempShapeConfig 1; bsTempShapeEnable and the channel flags random per frame): y10 stereoConfigIndex 1, decorrelator 0; y11 stereoConfigIndex 2 with 8 residual bands, decorrelator 1.
# usac_y12, y13: temporal shaping GES (bsTempShapeConfig 2: bsTempShapeEnable, channel flags and the run length coded envelope): y12 stereoConfigIndex 1, bsEnvQuantMode 1, decorrelator 0; y13 stereoConfigIndex 2 with 8 residual bands, bsEnvQuantMode 0, decorrelator 1; 40 frames (the energies of GES are smoothed over about 10 frames).
# usac_y14, y15: transient steering TSD (bsTempShapeConfig 3: bsTsdEnable, a combinatorial codeword placing up to 8 transient slots and a 3 bit phase each): y14 stereoConfigIndex 1, decorrelator 0; y15 stereoConfigIndex 2 with 8 residual bands, decorrelator 1.
# usac_y16: stereoConfigIndex 2 with 8 residual bands and bsPseudoLr = 1 (the two core channels are an M / S pair rotated by 45 degrees: (a + b) / sqrt 2, (a - b) / sqrt 2 before eSBR and the upmix), decorrelator 0.
# usac_y17..y19: bsHighRateMode (--mps-hr: bsFramingType, up to four parameter sets per frame, EcData modes default / keep / new in pairs, smoothing of the upmix matrices over time, bsSmoothMode 0..3); the independent frames are made without smoothing (GEN_HR_NOSMOOTH_INDEP: the first frame of libxaac runs through a start-up path): y17 stereoConfigIndex 1, decorrelator 1; y18 stereoConfigIndex 2, decorrelator 0; y19 stereoConfigIndex 2 with 6 residual bands, decorrelator 1.
# usage: tests/data/make_usac_mps_gen_fixtures.sh
set -euo pipefail
cd "$(dirname "$0")/../.."
make -s build/sauti_usac_gen >/dev/null
W="$(mktemp -d "${TMPDIR:-/tmp}/sauti-mpsgen.XXXXXX")"; trap 'rm -rf -- "${W:?}"' EXIT
gen() { build/sauti_usac_gen "$W/$1" --channels 2 --frames "${7:-24}" --gain 110 --seed "$2" --sbr --sbr-simple 384 --common --rate 3 --mps "${5:-2}" --mps-res "$3" --mps-decorr "$4" ${6:-} >/dev/null; python3 -I tools/mux_raw_aus_mp4.py "$W/$1" "tests/data/usac_$1.m4a" 2048; }
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
gen y12 81 0 0 1 "--mps-ges 1 --mps-envq 1" 40
gen y13 82 8 1 2 "--mps-ges 1 --mps-envq 0" 40
gen y14 101 0 0 1 "--mps-tsd 1"
gen y15 104 8 1 2 "--mps-tsd 1"
gen y16 111 8 0 2 "--mps-pseudo-lr"
export GEN_HR_NOSMOOTH_INDEP=1
gen y17 131 0 1 1 "--mps-hr"
gen y18 132 0 0 2 "--mps-hr"
gen y19 133 6 1 2 "--mps-hr"
