#!/usr/bin/env bash
# Re-entrancy of the native AAC decoders under ThreadSanitizer: the same streams (LC mono/stereo/5.1, HE-AAC v1/v2, M4A, LOAS, ADIF) decoded at the same time by several
# threads must give bit-identical PCM (also after a seek) and no TSan report.  Streams are made from a synthetic signal with fdkaac and FFmpeg (no user data).
# usage: scripts/test_aac_threads.sh [THREADS=6] [ROUNDS=2]      SKIP (exit 0, stated loudly) without fdkaac / FFmpeg.
set -euo pipefail
cd "$(dirname "$0")/.."
T="${1:-6}"; R="${2:-2}"
FDK="${SAUTI_FDKAAC:-$HOME/Sauti-tools/fdkaac/fdkaac}"; FF="${SAUTI_FFMPEG_INSTR:-$HOME/Sauti-tools/ffmpeg-sbr54w/ffmpeg}"
[ -x "$FDK" ] && [ -x "$FF" ] || { echo "SKIP: need fdkaac ($FDK) and FFmpeg ($FF)"; exit 0; }
if make -s build/test_aac_threads_tsan >/dev/null 2>&1; then BIN=build/test_aac_threads_tsan; echo "sanitizer: ThreadSanitizer"
else make -s build/test_aac_threads_asan >/dev/null; BIN=build/test_aac_threads_asan; echo "NOTE: no ThreadSanitizer runtime here (package libtsan): AddressSanitizer + bit-identical PCM comparison only; data races without corruption are NOT ruled out"; fi
W="$(mktemp -d "${TMPDIR:-/tmp}/sauti-thr.XXXXXX")"; trap 'rm -rf -- "$W"' EXIT
python3 -I - "$W/in.wav" "$W/in6.wav" <<'PY'
import sys,wave
import numpy as np
sr=48000; t=np.arange(sr*4)/sr; rng=np.random.default_rng(3)
for path,ch in ((sys.argv[1],2),(sys.argv[2],6)):
    x=np.zeros((len(t),ch))
    for c in range(ch): x[:,c]=0.25*np.sin(2*np.pi*(200*1.41**c)*t+c)+0.03*rng.standard_normal(len(t))
    w=wave.open(path,'wb'); w.setnchannels(ch); w.setsampwidth(2); w.setframerate(sr); w.writeframes((np.clip(x,-1,1)*32767).astype('<i2').tobytes()); w.close()
PY
"$FDK" -p 2 -b 96000 -f 2 -o "$W/lc.aac" "$W/in.wav" >/dev/null 2>&1
"$FDK" -p 5 -b 40000 -f 2 -o "$W/he1.aac" "$W/in.wav" >/dev/null 2>&1
"$FDK" -p 29 -b 48000 -f 2 -o "$W/he2.aac" "$W/in.wav" >/dev/null 2>&1
"$FDK" -p 5 -b 40000 -f 0 -o "$W/he1.m4a" "$W/in.wav" >/dev/null 2>&1
"$FDK" -p 5 -b 40000 -f 10 --header-period 1 -o "$W/he1.loas" "$W/in.wav" >/dev/null 2>&1
"$FF" -v error -y -i "$W/in6.wav" -af "channelmap=map=0|1|2|3|4|5:channel_layout=5.1" -c:a aac -b:a 384k -f adts "$W/m51.aac" 2>/dev/null
python3 -I - "$W" <<'PY'
import sys
w=sys.argv[1]
d=open(w+'/he1.aac','rb').read(); pos=0; blocks=[]
while pos+7<=len(d):
    ln=((d[pos+3]&3)<<11)|(d[pos+4]<<3)|(d[pos+5]>>5); blocks.append(d[pos+7:pos+ln]); pos+=ln
sfi=(d[2]>>2)&15
bits=''.join(format(v,'0%db'%n) for n,v in ((1,0),(1,0),(1,0),(1,1),(23,64000),(4,0),(4,0),(2,1),(4,sfi),(4,1),(4,0),(4,0),(2,0),(3,0),(4,0),(1,0),(1,0),(1,0),(1,1),(4,0)))
while len(bits)%8: bits+='0'
bits+='00000000'
open(w+'/he1.adif','wb').write(b'ADIF'+int(bits,2).to_bytes(len(bits)//8,'big')+b''.join(blocks))
PY
"$BIN" "$T" "$R" "$W/lc.aac" "$W/he1.aac" "$W/he2.aac" "$W/he1.m4a" "$W/he1.loas" "$W/he1.adif" "$W/m51.aac"
