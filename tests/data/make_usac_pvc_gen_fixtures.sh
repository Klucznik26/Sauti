#!/usr/bin/env bash
# Regenerates tests/data/usac_v1..v3.m4a: streams of tools/sauti_usac_gen.c (random but valid syntax) with predictive vector coding (PVC) in the eSBR configuration, mono, eSBR 2:1.  They carry what the encoder of
# libxaac never writes into a PVC frame: a variable length grid (bs_var_len_hf 1..3), sinusoids with bs_sine_position (also a sine that runs on into the first slots of the next frame), all pvc_id codings
# (bs_div_mode 0..7, bs_ns_mode), both prediction modes (1 and 2), and (v2) switches between classic and PVC frames.  Against libxaac's decoder the PVC internals agree frame by frame (predictor output within
# 1e-3, gains / noise / tone amplitudes of every time slot within a few 1e-3, checked with an instrumented copy of libxaac: docs/AAC_FAMILY_ASM.md SBR-114); the PCM of the random core is limited to 55-65 dB
# by libxaac's 16-bit output (the streams are very quiet) and by core frames libxaac decodes differently (loud random spectra), so the seeds below are the ones whose PCM agrees best.
# usage: tests/data/make_usac_pvc_gen_fixtures.sh   (needs `make build/sauti_usac_gen`)
set -euo pipefail
cd "$(dirname "$0")/../.."
make -s build/sauti_usac_gen >/dev/null
W="$(mktemp -d "${TMPDIR:-/tmp}/sauti-pvcgen.XXXXXX")"; trap 'rm -rf -- "${W:?}"' EXIT
gen() { build/sauti_usac_gen "$W/$1" --channels 1 --frames "$2" --gain 125 --seed "$3" --sbr --sbr-simple 384 --short --kbd --pvc "$4" >/dev/null; python3 -I tools/mux_raw_aus_mp4.py "$W/$1" "tests/data/usac_$1.m4a" 2048; }
gen v1 24 404 4        # modes 1 / 2 only, variable length grids, a carried-over sine
gen v2 28 506 1        # classic and PVC frames mixed (the first slots of a PVC frame after a classic frame that ran past its end)
gen v3 24 401 4        # three carried-over sines
