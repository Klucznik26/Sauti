#!/usr/bin/env bash
# SBR-83: the SBR channel decoder against an instrumented FFmpeg on streams with a real AAC-LC core and GENERATED SBR data
# (all four grid classes, smoothing on and off, random levels / inverse filtering / noise / harmonics, several band-and-patch geometries).
# usage: scripts/test_he_channel_remix_vs_ffmpeg.sh CORE.aac [FRAMES=200]
#   CORE.aac: mono ADTS stream with a 24 kHz core and no CRC (e.g. `fdkaac -p 5 -b 64 -f 2`); only its SCE elements are used.
# SKIP (exit 0, stated loudly) without the file or the instrumented FFmpeg: that is not a proof of agreement.
set -euo pipefail
cd "$(dirname "$0")/.."
CORE="${1:-}"; FRAMES="${2:-200}"
FF="${SAUTI_FFMPEG_INSTR:-$HOME/Sauti-tools/ffmpeg-sbr54w/ffmpeg}"
MIN="${HE_MIN_HF_DB:-100}"
[ -n "$CORE" ] && [ -r "$CORE" ] || { echo "SKIP: no core stream (usage: $0 CORE.aac [FRAMES])"; exit 0; }
[ -x "$FF" ] || { echo "SKIP: no instrumented FFmpeg ($FF); set SAUTI_FFMPEG_INSTR"; exit 0; }
make -s build/sauti_he_remix build/sauti_he_decode_cli >/dev/null
W="$(mktemp -d "${TMPDIR:-/tmp}/sauti-remix.XXXXXX")"; trap 'rm -rf -- "$W"' EXIT
fail=0; n=0
for header in 0 6 15 25 40; do
  for sm in 0 1; do
    for seed in 1 2; do
      SAUTI_REMIX_HEADER=$header build/sauti_he_remix "$CORE" "$W/s.aac" "$seed" "$sm" "$FRAMES" 0 1 >/dev/null || { echo "FAIL: remix"; exit 1; }
      SAUTI_SBR73_FFMPEG_SKIP_PS=1 "$FF" -v error -y -i "$W/s.aac" -f f32le -ac 2 "$W/ref.f32" >/dev/null 2>&1 || true
      [ -s "$W/ref.f32" ] || { echo "SKIP: this FFmpeg produced no PCM"; exit 0; }
      build/sauti_he_decode_cli "$W/s.aac" "$W/ours.f32" "$FRAMES" >/dev/null || { echo "FAIL header=$header smoothing_mode=$sm seed=$seed: decoder rejected"; fail=1; continue; }
      out=$(python3 -I tools/sauti_he_pcm_vs_ffmpeg.py "$W/ref.f32" "$W/ours.f32" "$MIN" "$MIN" | head -1) && rc=0 || rc=$?
      n=$((n+1)); echo "header=$header smoothing_mode=$sm seed=$seed: $out"
      [ "$rc" = 0 ] || fail=1
    done
  done
done
[ "$fail" = 0 ] && { echo "WYNIK: ZGODNE ($n strumieni)"; exit 0; } || { echo "WYNIK: NIEZGODNE"; exit 1; }
