#!/usr/bin/env bash
# Regenerates tests/data/usac_t1.m4a: a stream of tools/sauti_usac_gen.c (random but valid syntax, eSBR 2:1 with inter-subband temporal envelope shaping: bs_temp_shape / bs_inter_temp_shape_mode after every
# envelope).  libxaac's encoder never selects a shaping mode, so this is the only stream with inter-TES.  Against libxaac's decoder the shaping is checked by difference: the same stream decoded without the
# shaping is 14.9 dB from libxaac's output, with it 41.0 dB (the generator's random streams reach about 40 dB against libxaac with or without inter-TES; FDK and FFmpeg have no inter-TES).
# usage: tests/data/make_usac_tes_fixture.sh   (needs `make build/sauti_usac_gen`)
set -euo pipefail
cd "$(dirname "$0")/../.."
make -s build/sauti_usac_gen >/dev/null
W="$(mktemp -d "${TMPDIR:-/tmp}/sauti-tes.XXXXXX")"; trap 'rm -rf -- "${W:?}"' EXIT
build/sauti_usac_gen "$W/t1" --channels 1 --frames 24 --gain 125 --seed 201 --sbr --sbr-simple 388 --short --kbd --intertes >/dev/null
python3 -I tools/mux_raw_aus_mp4.py "$W/t1" tests/data/usac_t1.m4a 2048
