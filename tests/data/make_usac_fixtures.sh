#!/usr/bin/env bash
# Regenerates tests/test_usac_vectors.h (the AudioSpecificConfig and the access units of a few USAC streams) from the repository's own stream generator tools/sauti_usac_gen.c (random quantised
# spectra written with the arithmetic coder of ISO/IEC 23003-3).  NOT from an encoder and NOT from user material; the repository carries the OUTPUT.
#   usac_m1   mono, long windows only                                  FFmpeg 138 dB, FDK ~90 dB (its noise floor)
#   usac_m2   mono, short windows + KBD + TNS                          FFmpeg 139 dB, FDK ~90 dB
#   usac_m3   mono, noise filling, short windows                       FDK only (FFmpeg seeds the first channel's noise generator with 0 and skips it at noise_level 0)
#   usac_s1   stereo, common window, M/S, short windows, KBD            FFmpeg 139 dB, FDK ~91 dB
#   usac_s2   stereo, independent windows, TNS, dense (escape levels)   FDK only (FFmpeg never applies TNS of independent windows)
#   usac_m4   mono 24 kHz (other band tables), short windows, TNS    FFmpeg, FDK
#   usac_m5   mono, KBD already in the first frame                    FDK only (FFmpeg takes a sine window as the previous one)
#   usac_s3   stereo, common window, M/S, common TNS on L/R (tns_on_lr = 1), KBD   FFmpeg, FDK
#   usac_s4   stereo, noise filling with empty bands, M/S              FDK only (noise generator, see usac_m3)
#   usac_c1..c4 complex stereo prediction (ms_mask_present = 3; prediction in both directions, real and complex coefficients, delta coding over time / over frequency, the previous frame's downmix, long and short windows, TNS on L/R, noise filling): FDK only (FFmpeg takes the downmix of unused bands the wrong way round)
#   usac_c5 60 blocks, full max_sfb (the MDST filter reaches the top line), many window sequence changes: FDK only
#   usac_e1..e4 eSBR 2:1 (core 24 kHz, output 48 kHz; 2048 samples per block): e1 mono and e2 stereo (coupled and uncoupled pairs, M/S) with every grid class, time / frequency differential coding, default headers, no sinusoids and constant xover / amp_res: FFmpeg ~120 dB; e3 mono and e4 stereo with complex prediction: explicit headers, header / SbrInfo changes, sinusoids, noise filling: FDK only (FFmpeg reads two stray bits in an explicit USAC header, ignores xover changes, mishandles some sinusoids); usac_elong 100 blocks of e1 type for the engine's seek tests
#   usac_e5 mono eSBR whose DEFAULT header (in the configuration) is unusual: alter_scale 0, 3 noise bands, limiter 1 / 1, bs_interpol_freq 0, smoothing off: FFmpeg ~114 dB; bs_interpol_freq = 0 was ignored by the SBR chain (13 dB against FDK) before this stream
#   usac_e6 / e7 EXPLICIT headers repeated at every independent frame: e6 without any extra field (header_extra 0 / 0: the defaults), e7 with both extras and all fields different (limiter bands 3, gains 1, interpolation 0, smoothing 1; freq_scale 3, noise bands 1; start 5 / stop 9): FDK only
#   usac_e8 / e9 a header that CHANGES in the middle of the stream (SBR_HDR2 applies from the second header on: noise_bands 2 -> 1, a different number of noise bands, so an SBR reset) with sinusoids and amp_res changes, xover fixed: e8 mono, e9 stereo with coupled and uncoupled pairs. FDK only (FFmpeg
#             refuses one noise band: Invalid vDk0). Pins two behaviours of the reference decoder: the inverse-filter bandwidth factors and previous modes are NOT cleared when the number of noise bands changes (27.9 -> 43.9 dB), and the
#             sine phase index is NOT reset by an SBR reset (only the noise index is: 1-17 -> 41 dB on random header changes)
#   usac_k5 / k6 / k7 / k6long channel configurations 5, 6, 7 (5.0, 5.1, 7.1; SCE / CPE / LFE elements): k5 is FFmpeg-friendly (no noise filling, no TNS: FFmpeg 139 dB on every channel); k6 / k7 carry noise filling, TNS on L/R, M/S (k7: complex prediction): FDK reads only configurations 1 and 2, so every element is
#             checked as a standalone mono / stereo stream (SAUTI_USAC_GEN_ELEMS: the element's own bits; an LFE behind core_mode 0 and tns_data_present 0), see scripts/test_usac_vs_ffmpeg.sh; FFmpeg reads an LFE like an SCE (core_mode, TNS bits) and fails on k6 / k7; k6long is 70 blocks for the engine (seek over a checkpoint)
#   usac_k6bad 3 blocks of configuration 6 whose LFE uses an eight-short window from block 1 on (illegal): block 1 must be refused
#   usac_m1 / m3 / s1 / c1 / e2 / k6 / k6long .loas the same access units in LOAS / LATM (the StreamMuxConfig in every element); usac_k6_same.loas with the configuration only in the first element
#   usac_long mono, 150 frames (the checkpoints of the engine, every 64 blocks, carry the arithmetic context, noise generator, window overlap and shape): FDK only (noise)
#   usac_m2 also has TNS filters with coef_compress = 1; usac_m3 has entirely empty bands (noise_offset)
# usage: tests/data/make_usac_fixtures.sh   (needs python3 and a built build/sauti_usac_gen: make build/sauti_usac_gen)
set -euo pipefail
cd "$(dirname "$0")"
GEN=../../build/sauti_usac_gen
[ -x "$GEN" ] || { echo "build the generator first: make build/sauti_usac_gen"; exit 1; }
W="$(mktemp -d "${TMPDIR:-/tmp}/sauti-usac.XXXXXX")"; trap 'rm -rf -- "${W:?}"' EXIT
"$GEN" "$W/m1" --channels 1 --frames 12 --gain 125 --seed 71 >/dev/null
"$GEN" "$W/m2" --channels 1 --frames 12 --gain 125 --seed 72 --short --kbd --tns-compress >/dev/null
"$GEN" "$W/m3" --channels 1 --frames 12 --gain 125 --seed 73 --noise-fill --short --zero-bands >/dev/null
"$GEN" "$W/m4" --channels 1 --frames 12 --gain 125 --seed 76 --rate 6 --short --tns >/dev/null
"$GEN" "$W/m5" --channels 1 --frames 12 --gain 125 --seed 77 --short --kbd-first >/dev/null
"$GEN" "$W/s1" --channels 2 --frames 12 --gain 125 --seed 74 --ms --short --kbd >/dev/null
"$GEN" "$W/s2" --channels 2 --frames 12 --gain 75 --seed 75 --tns --short --dense >/dev/null
"$GEN" "$W/s3" --channels 2 --frames 12 --gain 125 --seed 78 --ms --tns --on-lr 1 --short --kbd >/dev/null
"$GEN" "$W/c1" --channels 2 --frames 24 --gain 125 --seed 91 --cplx >/dev/null
"$GEN" "$W/c2" --channels 2 --frames 24 --gain 125 --seed 92 --cplx --short --kbd >/dev/null
"$GEN" "$W/c3" --channels 2 --frames 24 --gain 125 --seed 94 --cplx --tns --on-lr 1 --short --kbd >/dev/null
"$GEN" "$W/c4" --channels 2 --frames 24 --gain 125 --seed 96 --cplx --short --kbd --noise-fill --zero-bands >/dev/null
"$GEN" "$W/c5" --channels 2 --frames 60 --gain 105 --seed 97 --cplx --short --kbd --full-sfb >/dev/null
"$GEN" "$W/e1" --channels 1 --frames 24 --gain 125 --seed 101 --sbr --sbr-simple 388 --short --kbd --tns >/dev/null
"$GEN" "$W/e2" --channels 2 --frames 24 --gain 125 --seed 102 --sbr --sbr-simple 900 --ms --short --kbd >/dev/null
"$GEN" "$W/e3" --channels 1 --frames 24 --gain 125 --seed 103 --sbr --sbr-simple 4096 --noise-fill --short --kbd >/dev/null
"$GEN" "$W/e4" --channels 2 --frames 24 --gain 110 --seed 107 --sbr --sbr-simple 4096 --cplx --short --kbd >/dev/null
SBR_CFG=5,9,2,0,3,1,3,0,1 "$GEN" "$W/e5" --channels 1 --frames 24 --gain 118 --seed 106 --sbr --sbr-simple $((128 + 256 + 4 + 512)) --short --kbd >/dev/null
SBR_HDR=6,9,2,1,2,2,2,1,1 "$GEN" "$W/e6" --channels 1 --frames 24 --gain 118 --seed 108 --sbr --sbr-simple $((4096 + 16 + 8)) --short --kbd >/dev/null
SBR_HDR=5,9,3,1,1,3,1,0,1 "$GEN" "$W/e7" --channels 2 --frames 24 --gain 118 --seed 109 --sbr --sbr-simple $((4096 + 16 + 8)) --ms --short --kbd >/dev/null
SBR_HDR2=5,9,2,1,1,2,2,1,1 "$GEN" "$W/e8" --channels 1 --frames 24 --gain 118 --seed 312 --sbr --sbr-simple 65536 --short --kbd >/dev/null
SBR_HDR2=5,9,2,1,1,2,2,1,1 "$GEN" "$W/e9" --channels 2 --frames 24 --gain 118 --seed 401 --sbr --sbr-simple 65536 --short --kbd >/dev/null
"$GEN" "$W/k5" --config 5 --frames 12 --gain 125 --seed 811 --ms --short --kbd >/dev/null
"$GEN" "$W/k6" --config 6 --frames 12 --gain 100 --seed 812 --ms --noise-fill --tns --on-lr 1 --short --kbd >/dev/null
"$GEN" "$W/k7" --config 7 --frames 12 --gain 100 --seed 813 --cplx --noise-fill --tns --on-lr 1 --short --kbd >/dev/null
"$GEN" "$W/k6bad" --config 6 --frames 3 --gain 100 --seed 815 --lfe-short >/dev/null
"$GEN" "$W/k6long" --config 6 --frames 70 --gain 100 --seed 814 --ms --noise-fill --tns --on-lr 1 --short --kbd >/dev/null
"$GEN" "$W/elong" --channels 1 --frames 100 --gain 125 --seed 105 --sbr --sbr-simple 388 --short --kbd >/dev/null
"$GEN" "$W/long" --channels 1 --frames 150 --gain 118 --seed 80 --noise-fill --zero-bands --short --kbd --tns >/dev/null
"$GEN" "$W/s4" --channels 2 --frames 12 --gain 142 --seed 79 --ms --noise-fill --zero-bands --short --kbd >/dev/null
for n in m1 m2 m3 m4 m5 s1 s2 s3 s4 c1 c2 c3 c4 c5 long; do python3 -I ../../tools/mux_raw_aus_mp4.py "$W/$n" "usac_$n.m4a" 1024; done
for n in k5 k6 k7 k6long; do python3 -I ../../tools/mux_raw_aus_mp4.py "$W/$n" "usac_$n.m4a" 1024; done
for n in m1 m3 s1 c1 e2 k6 k6long; do python3 -I ../../tools/mux_raw_aus_loas.py "$W/$n" "usac_$n.loas"; done          # LOAS / LATM: the same access units, the StreamMuxConfig in every element
python3 -I ../../tools/mux_raw_aus_loas.py "$W/k6" usac_k6_same.loas same        # ... and only in the first one (useSameStreamMux = 1)
for n in e1 e2 e3 e4 e5 e6 e7 e8 e9 elong; do python3 -I ../../tools/mux_raw_aus_mp4.py "$W/$n" "usac_$n.m4a" 2048; done
python3 -I - "$W" ../test_usac_vectors.h <<'PY'
import struct, sys
d, out = sys.argv[1], sys.argv[2]
text = ["/* Generated by tests/data/make_usac_fixtures.sh: the AudioSpecificConfig and the access units of nine USAC streams (tools/sauti_usac_gen.c). */\n"]
for name in ("m1", "m2", "m3", "m4", "m5", "s1", "s2", "s3", "s4", "c1", "c2", "c3", "c4", "c5", "e1", "e2", "e3", "e4", "e5", "e6", "e7", "e8", "e9"):
    asc = open("%s/%s.asc" % (d, name), "rb").read(); raw = open("%s/%s.aus" % (d, name), "rb").read(); aus = []; p = 0
    while p + 4 <= len(raw):
        n = struct.unpack('>I', raw[p:p+4])[0]; aus.append(raw[p+4:p+4+n]); p += 4 + n
    text.append("static const uint8_t usac_%s_asc[] = {%s};\n" % (name, ",".join("0x%02x" % b for b in asc)))
    for i, a in enumerate(aus): text.append("static const uint8_t usac_%s_au%d[] = {%s};\n" % (name, i, ",".join("0x%02x" % b for b in a)))
    text.append("static const uint8_t *const usac_%s_aus[] = {%s};\nstatic const size_t usac_%s_n[] = {%s};\n" % (name, ",".join("usac_%s_au%d" % (name, i) for i in range(len(aus))), name, ",".join("sizeof usac_%s_au%d" % (name, i) for i in range(len(aus)))))
open(out, "w").write("".join(text))
PY
python3 -I - "$W" ../test_usac_mc_vectors.h <<'PY'
import struct, sys
d, out = sys.argv[1], sys.argv[2]
text = ["/* Generated by tests/data/make_usac_fixtures.sh: the AudioSpecificConfig and the access units of three multichannel USAC streams (tools/sauti_usac_gen.c). */\n"]
for name in ("k5", "k6", "k7", "k6bad"):
    asc = open("%s/%s.asc" % (d, name), "rb").read(); raw = open("%s/%s.aus" % (d, name), "rb").read(); aus = []; p = 0
    while p + 4 <= len(raw):
        n = struct.unpack('>I', raw[p:p+4])[0]; aus.append(raw[p+4:p+4+n]); p += 4 + n
    text.append("static const uint8_t usac_%s_asc[] = {%s};\n" % (name, ",".join("0x%02x" % b for b in asc)))
    for i, a in enumerate(aus): text.append("static const uint8_t usac_%s_au%d[] = {%s};\n" % (name, i, ",".join("0x%02x" % b for b in a)))
    text.append("static const uint8_t *const usac_%s_aus[] = {%s};\nstatic const size_t usac_%s_n[] = {%s};\n" % (name, ",".join("usac_%s_au%d" % (name, i) for i in range(len(aus))), name, ",".join("sizeof usac_%s_au%d" % (name, i) for i in range(len(aus)))))
open(out, "w").write("".join(text))
PY
