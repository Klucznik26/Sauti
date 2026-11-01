#!/bin/sh
# Real encoded fixtures, tested through Sauti's in-process decoder interface.
set -eu
if ! pkg-config --exists libavformat libavcodec libavutil libswresample; then
    echo "SKIP media fixtures: FFmpeg development libraries unavailable"
    exit 0
fi
if ! command -v ffmpeg >/dev/null 2>&1; then
    echo "SKIP media fixtures: ffmpeg CLI unavailable for fixture generation"
    exit 0
fi
mkdir -p build/audio-fixtures
ffmpeg -hide_banner -nostdin -loglevel error -y \
    -f lavfi -i "sine=frequency=440:sample_rate=44100:duration=0.4" \
    -ac 2 -c:a aac -b:a 128k build/audio-fixtures/aac.m4a
ffmpeg -hide_banner -nostdin -loglevel error -y \
    -f lavfi -i "sine=frequency=330:sample_rate=44100:duration=0.4" \
    -ac 2 -c:a aac -f adts build/audio-fixtures/aac.aac
ffmpeg -hide_banner -nostdin -loglevel error -y \
    -f lavfi -i "sine=frequency=660:sample_rate=44100:duration=0.4" \
    -ac 2 -c:a alac build/audio-fixtures/alac.m4a
ffmpeg -hide_banner -nostdin -loglevel error -y \
    -f lavfi -i "sine=frequency=550:sample_rate=48000:duration=0.4" \
    -ac 2 -c:a libopus -b:a 96k -f webm build/audio-fixtures/opus.webm
./build/test_decoder_ffmpeg build/audio-fixtures/aac.m4a aac
./build/test_decoder_ffmpeg build/audio-fixtures/aac.aac aac
./build/test_aac_syntax build/audio-fixtures/aac.aac
./build/test_decoder_ffmpeg build/audio-fixtures/alac.m4a alac
./build/test_decoder_ffmpeg build/audio-fixtures/opus.webm webm
