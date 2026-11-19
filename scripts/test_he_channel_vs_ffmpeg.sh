#!/usr/bin/env bash
# SBR-82: mono HE-AAC (v1 or v2 core) through the channel decoder vs an instrumented FFmpeg on a REAL file.
# usage: scripts/test_he_channel_vs_ffmpeg.sh FILE.aac [FRAMES=400]   (SAUTI_FFMPEG_INSTR=path to the instrumented ffmpeg)
# SKIP (exit 0, stated loudly) without the file or the instrumented FFmpeg: that is not a proof of agreement.
set -euo pipefail
cd "$(dirname "$0")/.."
FILE="${1:-}"; FRAMES="${2:-400}"
FF="${SAUTI_FFMPEG_INSTR:-$HOME/Sauti-tools/ffmpeg-sbr54w/ffmpeg}"
[ -n "$FILE" ] && [ -r "$FILE" ] || { echo "SKIP: no input file (usage: $0 FILE.aac [FRAMES])"; exit 0; }
[ -x "$FF" ] || { echo "SKIP: no instrumented FFmpeg ($FF); set SAUTI_FFMPEG_INSTR"; exit 0; }
make -s build/sauti_he_decode_cli >/dev/null
W="$(mktemp -d "${TMPDIR:-/tmp}/sauti-chan.XXXXXX")"; trap 'rm -rf -- "$W"' EXIT
python3 -I - "$FILE" "$FRAMES" "$W/cut.aac" <<'PY'
import sys
data=open(sys.argv[1],'rb').read(); n=int(sys.argv[2]); out=bytearray(); pos=0; cnt=0
while pos+7<=len(data) and cnt<n:
    if data[pos]!=0xFF or (data[pos+1]&0xF0)!=0xF0: pos+=1; continue
    ln=((data[pos+3]&3)<<11)|(data[pos+4]<<3)|(data[pos+5]>>5)
    if ln<7 or pos+ln>len(data): break
    out+=data[pos:pos+ln]; pos+=ln; cnt+=1
open(sys.argv[3],'wb').write(out); print("ADTS frames: %d"%cnt)
PY
# the instrumented FFmpeg skips parametric stereo when asked, so a v2 stream is compared on its mono SBR output
SAUTI_SBR73_FFMPEG_SKIP_PS=1 "$FF" -v error -y -i "$W/cut.aac" -f f32le -ac 2 "$W/ref.f32" >/dev/null 2>&1 || true
[ -s "$W/ref.f32" ] || { echo "SKIP: this FFmpeg produced no PCM"; exit 0; }
build/sauti_he_decode_cli "$W/cut.aac" "$W/ours.f32" "$FRAMES" || { echo "FAIL: channel decoder rejected the stream"; exit 1; }
python3 -I tools/sauti_he_pcm_vs_ffmpeg.py "$W/ref.f32" "$W/ours.f32" "${HE_MIN_HF_DB:-100}" "${HE_MIN_LOW_DB:-100}"
