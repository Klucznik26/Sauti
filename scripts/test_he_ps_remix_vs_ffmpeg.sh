#!/usr/bin/env bash
# SBR-89/90: parametric stereo (HE-AAC v2) against an instrumented FFmpeg on streams with a real AAC-LC core and GENERATED ps_data():
# IID/ICC in the 10/20/34-band modes (coarse and fine IID, mixed layouts), fixed and variable frame classes, frames without envelopes, and random
# mode changes in the middle of the stream (20 <-> 34 band layout, with values in the changing frame).  IPD/OPD are NOT in this set: the sign of the
# phase in the lowest hybrid bands is undecided (docs SBR-89).
# usage: scripts/test_he_ps_remix_vs_ffmpeg.sh CORE.aac [FRAMES=200]   (CORE.aac: mono 24 kHz core ADTS without CRC, e.g. `fdkaac -p 5 -b 64 -f 2`)
# SKIP (exit 0, stated loudly) without the file or the instrumented FFmpeg: that is not a proof of agreement.
set -euo pipefail
cd "$(dirname "$0")/.."
CORE="${1:-}"; FRAMES="${2:-200}"
FF="${SAUTI_FFMPEG_INSTR:-$HOME/Sauti-tools/ffmpeg-sbr54w/ffmpeg}"
MIN="${HE_MIN_DB:-100}"
[ -n "$CORE" ] && [ -r "$CORE" ] || { echo "SKIP: no core stream (usage: $0 CORE.aac [FRAMES])"; exit 0; }
[ -x "$FF" ] || { echo "SKIP: no instrumented FFmpeg ($FF); set SAUTI_FFMPEG_INSTR"; exit 0; }
make -s build/sauti_he_remix build/sauti_he_decode_cli >/dev/null
W="$(mktemp -d "${TMPDIR:-/tmp}/sauti-psremix.XXXXXX")"; trap 'rm -rf -- "$W"' EXIT
fail=0; n=0
check() { # name seed env...
  local name="$1" seed="$2"; shift 2
  env "$@" SAUTI_REMIX_PS=1 build/sauti_he_remix "$CORE" "$W/s.aac" "$seed" 1 "$FRAMES" 0 1 >/dev/null || { echo "FAIL $name: remix"; fail=1; return; }
  "$FF" -v error -y -i "$W/s.aac" -f f32le -ac 2 "$W/ref.f32" >/dev/null 2>&1 || true
  [ -s "$W/ref.f32" ] || { echo "SKIP: this FFmpeg produced no PCM"; exit 0; }
  SAUTI_SBR82_PS=1 build/sauti_he_decode_cli "$W/s.aac" "$W/ours.f32" "$FRAMES" >/dev/null 2>&1 || { echo "FAIL $name: decoder rejected the stream"; fail=1; return; }
  local out rc=0
  out=$(python3 -I tools/sauti_he_pcm_vs_ffmpeg.py "$W/ref.f32" "$W/ours.f32" "$MIN" "$MIN" stereo | tr '\n' ' ') || rc=$?
  n=$((n+1)); echo "$name: $out"
  [ "$rc" = 0 ] || fail=1
}
for im in 0 1 2 3 4 5; do check "iid_mode=$im icc_mode=$(( (im+1) % 6 ))" 3 SAUTI_REMIX_PS_IID_MODE=$im SAUTI_REMIX_PS_ICC_MODE=$(( (im+1) % 6 )); done
check "fixed frame class" 4 SAUTI_REMIX_PS_CLASS=0
check "variable frame class" 4 SAUTI_REMIX_PS_CLASS=1
check "30% frames without envelopes" 5 SAUTI_REMIX_PS_NOENV=30
for seed in 3 4 5 6; do check "mode change every 5 frames, seed $seed" "$seed" SAUTI_REMIX_PS_CHANGE=5; done
[ "$fail" = 0 ] && { echo "WYNIK: ZGODNE ($n strumieni)"; exit 0; } || { echo "WYNIK: NIEZGODNE"; exit 1; }
