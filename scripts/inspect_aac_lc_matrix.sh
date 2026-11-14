#!/bin/sh
# AAC-23: deterministic real-world-like diagnostics for MONO AAC-LC.
# Reports conformance candidates without silently turning differences into
# a green build. Decoder coverage and PCM errors are printed for each case.
set -eu

if ! command -v ffmpeg >/dev/null 2>&1; then
    echo "SKIP AAC-23 fixtures: ffmpeg CLI unavailable"
    exit 0
fi
if ! command -v python3 >/dev/null 2>&1; then
    echo "SKIP AAC-23 fixtures: python3 unavailable"
    exit 0
fi

root=build/audio-fixtures/aac-23
mkdir -p "$root"
python3 tools/generate_aac_fixtures.py "$root" 44100
for name in tone440 tone1000 multitone transient chirp; do
    echo "===== AAC-23: $name ====="
    ffmpeg -hide_banner -nostdin -loglevel error -y \
        -i "$root/$name.wav" -ac 1 -c:a aac -b:a 96k -f adts \
        "$root/$name.aac"
    ffmpeg -hide_banner -nostdin -loglevel error -y \
        -i "$root/$name.aac" -ac 1 -ar 44100 -f f32le \
        "$root/$name.ref.f32"
    ./build/test_aac_real_mono "$root/$name.aac" \
        "$root/$name.native.f32" "$root/$name.mask"
    python3 tools/compare_aac_pcm.py "$root/$name.native.f32" \
        "$root/$name.ref.f32" "$root/$name.mask"
done
echo "AAC-23 diagnostic matrix complete; inspect per-fixture PCM quality and coverage."
