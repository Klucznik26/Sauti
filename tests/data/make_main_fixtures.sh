#!/usr/bin/env bash
# Regenerates the AAC Main (object type 1: backward-adaptive prediction) fixtures from the repository's own stream generator (tools/sauti_aac_gen.c --main: random quantised spectra with the
# standard Huffman codebooks plus random predictor_data: prediction_used flags, reset groups) and the muxers tools/mux_raw_aus_mp4.py / mux_raw_aus_adts.py.  NOT from an encoder and NOT from user
# material; the repository carries the OUTPUT.  --level 136 keeps the peak below full scale.
#   main_st / main_mono / main_long   constant max_sfb (FFmpeg is a valid reference only then: it keeps stale prediction_used flags when max_sfb changes between frames)
#   main_var                          max_sfb varies (FAAD2 is the reference)
#   main_pns / main_pnsst             noise bands with prediction (mono / stereo without M/S) (FAAD2 resets the predictors of noise bands; the noise itself is 20 octaves quiet so its random sequence does not matter)
#   main_rNN                          one stereo file per sampling_frequency_index 0..11 (PRED_SFB_MAX differs per rate); main_r12.m4a (7350 Hz, no PRED_SFB_MAX) is a refusal fixture
#   main_sbr.m4a                      an SBR fill element in a Main stream (refused)
# main_st also as ADTS (.aac, profile 0) and, with FFmpeg's LATM muxer, as LOAS.
# usage: tests/data/make_main_fixtures.sh   (needs python3 and a built build/sauti_aac_gen: make build/sauti_aac_gen; ffmpeg only for the LOAS file)
set -euo pipefail
cd "$(dirname "$0")"
GEN=../../build/sauti_aac_gen; MUX=../../tools/mux_raw_aus_mp4.py; ADTS=../../tools/mux_raw_aus_adts.py
[ -x "$GEN" ] || { echo "build the generator first: make build/sauti_aac_gen"; exit 1; }
W="$(mktemp -d "${TMPDIR:-/tmp}/sauti-main.XXXXXX")"; trap 'rm -rf -- "${W:?}"' EXIT
g() { n="$1"; shift; "$GEN" "$W/$n" --main --level 136 "$@" >/dev/null; python3 -I "$MUX" "$W/$n" "$n.m4a" 1024; }
g main_st    --channels 2 --rate 3 --frames 40  --full-sfb --ms --tns --short --kbd --seed 31
g main_mono  --channels 1 --rate 4 --frames 40  --full-sfb --tns --short --kbd --seed 32
g main_long  --channels 1 --rate 3 --frames 150 --full-sfb --tns --short --kbd --seed 35
g main_var   --channels 2 --rate 3 --frames 40  --ms --tns --short --kbd --seed 33
g main_pns   --channels 1 --rate 4 --frames 40  --full-sfb --pns-den 2 --pns-quiet --seed 34
g main_pnsst --channels 2 --rate 3 --frames 40  --full-sfb --pns-den 3 --pns-quiet --tns --seed 37
for r in 0 1 2 3 4 5 6 7 8 9 10 11 12; do g "main_r$(printf %02d "$r")" --channels 2 --rate "$r" --frames 14 --full-sfb --ms --seed $((40 + r)); done
# multichannel (channel_configuration 3..7: 3.0, 4.0, 5.0, 5.1, 7.1): main_c3_long has 70 frames so that the exact-seek test crosses a checkpoint (64 frames) that carries the per-channel state
# (the generator's small --amp keeps the files small); ADTS with the error check (protection_absent = 0; main_st_crc.aac) and with a wrong CRC in access unit 7 (main_st_crcbad.aac); ADIF (main_st.adif)
g main_c3_long --config 3 --frames 70 --amp 3 --full-sfb --ms --tns --short --kbd --seed 71
for cc in 3 4 5 6 7; do g main_c$cc --config $cc --frames 12 --amp 3 --full-sfb --ms --tns --short --kbd --seed $((80 + cc)); done
# 5.1 with audible noise substitution (the seed of the noise generator runs through the elements of a frame AND from frame to frame; no M/S: FFmpeg has no correlated noise there; no predictor_data: FFmpeg keeps the predictors of noise bands)
g main_c6_pns --config 6 --frames 12 --amp 3 --full-sfb --pns-den 3 --pred-mode 1 --tns --short --kbd --seed 87
python3 -I "$ADTS" "$W/main_st" main_st_crc.aac --crc
python3 -I "$ADTS" "$W/main_st" main_st_crcbad.aac --crc --crc-corrupt 7
python3 -I ../../tools/mux_raw_aus_adif.py "$W/main_st" main_st.adif
# Main with frameLengthFlag = 1 (960 lines per frame; FFmpeg is the only reference that decodes it: the standard text for the predictor at 960 lines could not be checked against a second decoder)
for n in main960_mono:1:4 main960_st:2:3; do
  name="${n%%:*}"; rest="${n#*:}"; ch="${rest%%:*}"; rate="${rest#*:}"
  "$GEN" "$W/$name" --main --level 136 --frame 960 --channels "$ch" --rate "$rate" --frames 40 --full-sfb --ms --tns --short --kbd --seed $((90 + ch)) >/dev/null
  python3 -I "$MUX" "$W/$name" "$name.m4a" 960
done
if command -v ffmpeg >/dev/null; then ffmpeg -v error -y -i main960_st.m4a -c copy -f latm main960_st.loas || echo "ffmpeg cannot write LOAS for 960"; fi
python3 -I "$ADTS" "$W/main_st" main_st.aac
if command -v ffmpeg >/dev/null; then ffmpeg -v error -y -i main_st.m4a -c copy -f latm main_st.loas; else echo "no ffmpeg: main_st.loas not regenerated"; fi
# an SBR fill element (extension_type 13) before ID_END of every access unit of a Main stream: must be refused, not decoded as plain Main
"$GEN" "$W/main_sbr" --main --level 136 --channels 2 --rate 3 --frames 8 --full-sfb --seed 36 >/dev/null
python3 -I - "$W" <<'PY'
import struct, sys
d = sys.argv[1]; raw = open(d + "/main_sbr.aus", "rb").read(); out = b""; p = 0
while p + 4 <= len(raw):
    n = struct.unpack('>I', raw[p:p+4])[0]; au = raw[p+4:p+4+n]; p += 4 + n
    body = ''.join(format(b, '08b') for b in au).rstrip('0')[:-3]
    nb = body + '110' + format(2, '04b') + format(13, '04b') + '0' * 12 + '111'
    nb += '0' * (-len(nb) % 8); b = int(nb, 2).to_bytes(len(nb) // 8, 'big'); out += struct.pack('>I', len(b)) + b
open(d + "/main_sbr.aus", "wb").write(out)
PY
python3 -I "$MUX" "$W/main_sbr" main_sbr.m4a 1024
python3 -I - "$W" ../test_aac_main_vectors.h <<'PY'
import struct, sys
d, out = sys.argv[1], sys.argv[2]
text = ["/* Generated by tests/data/make_main_fixtures.sh: the AudioSpecificConfig and the first access units of main_st.m4a / main_mono.m4a. */\n"]
for name in ("st", "mono", "c6"):
    asc = open("%s/main_%s.asc" % (d, name), "rb").read(); raw = open("%s/main_%s.aus" % (d, name), "rb").read(); aus = []; p = 0
    while p + 4 <= len(raw):
        n = struct.unpack('>I', raw[p:p+4])[0]; aus.append(raw[p+4:p+4+n]); p += 4 + n
    text.append("static const uint8_t main_%s_asc[] = {%s};\n" % (name, ",".join("0x%02x" % b for b in asc)))
    for i in range(6 if name != 'c6' else 3): text.append("static const uint8_t main_%s_au%d[] = {%s};\n" % (name, i, ",".join("0x%02x" % b for b in aus[i])))
open(out, "w").write("".join(text))
PY
