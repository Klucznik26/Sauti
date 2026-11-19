#!/usr/bin/env bash
# SBR-86: the PUBLIC HE-AAC adapters (registry) on a real file: PCM against an instrumented FFmpeg, the number of samples (including the last parametric-stereo
# frame), and 200 seeks that must reproduce the sequential read bit for bit.
# usage: scripts/test_he_adapter_vs_ffmpeg.sh FILE.aac     (ADTS; mono v1, v2 with PS, or stereo v1)    SAUTI_FFMPEG_INSTR=path to the instrumented ffmpeg
# SKIP (exit 0, stated loudly) without the file or the instrumented FFmpeg: that is not a proof of agreement.
set -euo pipefail
cd "$(dirname "$0")/.."
FILE="${1:-}"
FF="${SAUTI_FFMPEG_INSTR:-$HOME/Sauti-tools/ffmpeg-sbr54w/ffmpeg}"
[ -n "$FILE" ] && [ -r "$FILE" ] || { echo "SKIP: no input file (usage: $0 FILE.aac)"; exit 0; }
[ -x "$FF" ] || { echo "SKIP: no instrumented FFmpeg ($FF); set SAUTI_FFMPEG_INSTR"; exit 0; }
make -s build/sauti_aac_adapter_cli build/sauti_aac_seek_check >/dev/null
W="$(mktemp -d "${TMPDIR:-/tmp}/sauti-adapter.XXXXXX")"; trap 'rm -rf -- "$W"' EXIT
info=$(build/sauti_aac_adapter_cli "$FILE" "$W/ours.f32") || { echo "FAIL: no adapter accepted the file ($info)"; exit 1; }
echo "$info"
ch=$(echo "$info" | sed -n 's/.*channels=\([0-9]*\).*/\1/p')
if [ "$ch" = 1 ]; then   # mono SBR output: the reference skips parametric stereo
  SAUTI_SBR73_FFMPEG_SKIP_PS=1 "$FF" -v error -y -i "$FILE" -f f32le -ac 2 "$W/ref.f32" >/dev/null 2>&1 || true
  mode=""
else
  "$FF" -v error -y -i "$FILE" -f f32le -ac 2 "$W/ref.f32" >/dev/null 2>&1 || true
  mode="stereo"
fi
[ -s "$W/ref.f32" ] || { echo "SKIP: this FFmpeg produced no PCM"; exit 0; }
python3 -I - "$W/ref.f32" "$W/ours.f32" "$ch" <<'PY' || exit 1
import sys,os
a=os.path.getsize(sys.argv[1])//4//2; b=os.path.getsize(sys.argv[2])//4//int(sys.argv[3])
print("frames: reference %d, ours %d"%(a,b))
sys.exit(0 if a==b else 1)
PY
python3 -I tools/sauti_he_pcm_vs_ffmpeg.py "$W/ref.f32" "$W/ours.f32" "${HE_MIN_HF_DB:-100}" "${HE_MIN_LOW_DB:-100}" $mode || exit 1
build/sauti_aac_seek_check "$FILE" 200
