#!/usr/bin/env bash
# SBR-99: AAC-LC variants against FFmpeg on SYNTHETIC signals (one tone of its own frequency and amplitude per channel, so a swapped channel cannot hide):
# channel layouts mono, stereo, 3.0, 5.0 (PCE), 5.1, 7.1 (PCE) and sample rates 8000 ... 96000 Hz.  Channels are compared in the SAME order as FFmpeg's output.
# usage: scripts/test_aac_lc_variants_vs_ffmpeg.sh   (needs the instrumented or any FFmpeg with the native aac encoder: SAUTI_FFMPEG_INSTR=path, python3 with numpy)
# SKIP (exit 0, stated loudly) without FFmpeg: that is not a proof of agreement.
set -euo pipefail
cd "$(dirname "$0")/.."
FF="${SAUTI_FFMPEG_INSTR:-$HOME/Sauti-tools/ffmpeg-sbr54w/ffmpeg}"
MIN="${LC_MIN_DB:-100}"
[ -x "$FF" ] || { echo "SKIP: no FFmpeg ($FF); set SAUTI_FFMPEG_INSTR"; exit 0; }
make -s build/sauti_aac_adapter_cli >/dev/null
W="$(mktemp -d "${TMPDIR:-/tmp}/sauti-lcvar.XXXXXX")"; trap 'rm -rf -- "$W"' EXIT
gen() { # channels rate seconds out.wav
python3 -I - "$1" "$2" "$3" "$4" <<'PY'
import sys,wave
import numpy as np
ch,sr,sec,path=int(sys.argv[1]),int(sys.argv[2]),float(sys.argv[3]),sys.argv[4]
t=np.arange(int(sr*sec))/sr; rng=np.random.default_rng(7)
x=np.zeros((len(t),ch))
for c in range(ch):
    f=min(180.0*(1.37**c),sr*0.3); a=0.30-0.03*c
    x[:,c]=a*np.sin(2*np.pi*f*t+c)+0.02*a*rng.standard_normal(len(t))*(c+1)/ch
w=wave.open(path,'wb'); w.setnchannels(ch); w.setsampwidth(2); w.setframerate(sr)
w.writeframes((np.clip(x,-1,1)*32767).astype('<i2').tobytes()); w.close()
PY
}
fail=0; n=0
check() { # name channels rate layout bitrate extra...
  local name="$1" ch="$2" sr="$3" layout="$4" br="$5"; shift 5
  gen "$ch" "$sr" 5 "$W/in.wav"
  "$FF" -v error -y -i "$W/in.wav" -af "channelmap=map=$(seq -s'|' 0 $((ch-1))):channel_layout=$layout" -c:a aac -b:a "$br" "$@" -f adts "$W/s.aac" 2>/dev/null || { echo "SKIP $name: this FFmpeg cannot encode it"; return; }
  build/sauti_aac_adapter_cli "$W/s.aac" "$W/ours.f32" >/dev/null 2>&1 || { echo "FAIL $name: no native adapter accepted the stream"; fail=1; return; }
  "$FF" -v error -y -i "$W/s.aac" -f f32le "$W/ref.f32" 2>/dev/null
  local out rc=0
  out=$(python3 -I tools/sauti_multichannel_compare.py "$W/ref.f32" "$W/ours.f32" "$ch" "$MIN") || rc=$?
  n=$((n+1)); echo "$name: $out"
  [ "$rc" = 0 ] || fail=1
}
check "mono 48000" 1 48000 mono 96k
check "stereo 48000" 2 48000 stereo 128k
check "3.0 48000" 3 48000 3.0 192k
check "5.0 PCE 48000" 5 48000 5.0 320k -aac_pce 1
check "5.1 48000" 6 48000 5.1 384k
check "7.1 PCE 48000" 8 48000 7.1 512k -aac_pce 1
for sr in 8000 11025 12000 16000 22050 24000 32000 44100 64000 88200 96000; do
  check "stereo $sr" 2 "$sr" stereo 96k
  check "5.1 $sr" 6 "$sr" 5.1 256k
done
[ "$fail" = 0 ] && { echo "WYNIK: ZGODNE ($n strumieni)"; exit 0; } || { echo "WYNIK: NIEZGODNE"; exit 1; }
