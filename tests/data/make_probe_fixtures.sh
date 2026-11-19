#!/usr/bin/env bash
# Regenerates the small synthetic fixtures of the NON-AAC formats used by the probe audit (tests/test_maktabasauti_probe_audit.c) and the parallel recognition test:
# real FLAC, Ogg Vorbis, MP3, WAV, AIFF, VOC, Opus, ALAC/M4A made from FFmpeg's generated tones (lavfi sine, 0.3 s, 22.05 kHz) -- NOT user material.  The encoders are used
# only here, once; the repository carries their OUTPUT, so CI needs neither FFmpeg nor lame.
# usage: tests/data/make_probe_fixtures.sh   (needs FFmpeg with libvorbis, libmp3lame, libopus, flac, alac, aiff and voc muxers)
set -euo pipefail
cd "$(dirname "$0")"
FF="${SAUTI_FFMPEG:-ffmpeg}"
SRC=(-f lavfi -i "sine=frequency=440:duration=0.3:sample_rate=22050" -f lavfi -i "sine=frequency=660:duration=0.3:sample_rate=22050" -filter_complex "[0:a][1:a]amerge=inputs=2[a]" -map "[a]")
run() { "$FF" -v error -y "${SRC[@]}" "$@"; }
run -c:a flac -compression_level 5 -bitexact probe_real.flac
run -c:a libvorbis -q:a 2 -bitexact -fflags +bitexact probe_real.ogg
run -c:a libmp3lame -b:a 64k -bitexact -fflags +bitexact probe_real.mp3
run -c:a pcm_s16le -bitexact probe_real.wav
run -c:a pcm_s16be -bitexact -f aiff probe_real.aiff
run -ac 1 -c:a pcm_u8 -bitexact -f voc probe_real.voc
run -c:a libopus -b:a 32k -bitexact -fflags +bitexact probe_real.opus
run -c:a alac -bitexact -fflags +bitexact probe_real.m4a
ls -l probe_real.* | awk '{print $5,$NF}'
