#!/usr/bin/env bash
# Regenerates the small synthetic AAC fixtures used by the thread test in CI (tests/test_aac_threads.c): a deterministic mix of tones and noise (numpy default_rng(3)), NOT user
# material.  The encoders are used only here, once; the repository carries their OUTPUT, so CI needs neither fdkaac nor FFmpeg.
# usage: tests/data/make_fixtures.sh   (needs fdkaac, FFmpeg with the native aac encoder, python3 with numpy)
set -euo pipefail
cd "$(dirname "$0")"
FDK="${SAUTI_FDKAAC:-$HOME/Sauti-tools/fdkaac/fdkaac}"; FF="${SAUTI_FFMPEG_INSTR:-$HOME/Sauti-tools/ffmpeg-sbr54w/ffmpeg}"
W="$(mktemp -d "${TMPDIR:-/tmp}/sauti-fx.XXXXXX")"; trap 'rm -rf -- "${W:?}"' EXIT
python3 -I - "$W/in.wav" "$W/in6.wav" <<'PY'
import sys,wave
import numpy as np
sr=48000; t=np.arange(sr*2)/sr; rng=np.random.default_rng(3)
for path,ch in ((sys.argv[1],2),(sys.argv[2],6)):
    x=np.zeros((len(t),ch))
    for c in range(ch): x[:,c]=0.25*np.sin(2*np.pi*(200*1.41**c)*t+c)+0.03*rng.standard_normal(len(t))
    w=wave.open(path,'wb'); w.setnchannels(ch); w.setsampwidth(2); w.setframerate(sr); w.writeframes((np.clip(x,-1,1)*32767).astype('<i2').tobytes()); w.close()
PY
"$FDK" -p 2  -b 96000 -f 2  -o lc.aac        "$W/in.wav" >/dev/null 2>&1
"$FDK" -p 5  -b 40000 -f 2  -o he1.aac       "$W/in.wav" >/dev/null 2>&1
"$FDK" -p 29 -b 48000 -f 2  -o he2.aac       "$W/in.wav" >/dev/null 2>&1
"$FDK" -p 5  -b 40000 -f 0 --no-timestamp -o he1.m4a "$W/in.wav" >/dev/null 2>&1
"$FDK" -p 5  -b 40000 -f 10 --header-period 1 -o he1.loas "$W/in.wav" >/dev/null 2>&1
"$FF" -v error -y -i "$W/in6.wav" -af "channelmap=map=0|1|2|3|4|5:channel_layout=5.1" -c:a aac -b:a 256k -f adts m51.aac 2>/dev/null
python3 -I - <<'PY'
d=open('he1.aac','rb').read(); pos=0; blocks=[]
while pos+7<=len(d):
    ln=((d[pos+3]&3)<<11)|(d[pos+4]<<3)|(d[pos+5]>>5); blocks.append(d[pos+7:pos+ln]); pos+=ln
sfi=(d[2]>>2)&15
bits=''.join(format(v,'0%db'%n) for n,v in ((1,0),(1,0),(1,0),(1,1),(23,64000),(4,0),(4,0),(2,1),(4,sfi),(4,1),(4,0),(4,0),(2,0),(3,0),(4,0),(1,0),(1,0),(1,0),(1,1),(4,0)))
while len(bits)%8: bits+='0'
bits+='00000000'
open('he1.adif','wb').write(b'ADIF'+int(bits,2).to_bytes(len(bits)//8,'big')+b''.join(blocks))
PY
# mono: AAC-LC mono i HE-AAC v1 mono (rdzen mono + SBR, bez PS), dodane przy etapie 2 (strumieniowy ADTS); osobne wejscie mono, to samo ziarno
python3 -I - "$W/in_mono.wav" <<'PY'
import sys,wave
import numpy as np
sr=48000; t=np.arange(sr*2)/sr; rng=np.random.default_rng(3)
x=0.25*np.sin(2*np.pi*200*t)+0.03*rng.standard_normal(len(t))
w=wave.open(sys.argv[1],'wb'); w.setnchannels(1); w.setsampwidth(2); w.setframerate(sr); w.writeframes((np.clip(x,-1,1)*32767).astype('<i2').tobytes()); w.close()
PY
"$FDK" -p 2  -b 64000 -f 2 -o lc_mono.aac  "$W/in_mono.wav" >/dev/null 2>&1
"$FDK" -p 5  -b 32000 -f 2 -o he1_mono.aac "$W/in_mono.wav" >/dev/null 2>&1
ls -l *.aac *.m4a *.loas *.adif | awk '{print $5,$9}'
