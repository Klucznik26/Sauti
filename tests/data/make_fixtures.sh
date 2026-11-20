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
# LOAS/LATM (etap 2: strumieniowy LOAS): LC stereo i mono (konfiguracja tylko w pierwszym elemencie), HE-AAC v2 (konfiguracja w kazdym elemencie)
"$FDK" -p 2  -b 96000 -f 10 -o lc.loas      "$W/in.wav"      >/dev/null 2>&1
"$FDK" -p 2  -b 64000 -f 10 -o lc_mono.loas "$W/in_mono.wav" >/dev/null 2>&1
"$FDK" -p 29 -b 48000 -f 10 --header-period 1 -o he2.loas "$W/in.wav" >/dev/null 2>&1
# ADIF z ramek ADTS LC (etap 2: strumieniowy ADIF): stereo (CPE) i mono (SCE), naglowek jak dla he1.adif (zmienna przeplywnosc, jeden PCE)
python3 -I - <<'PY'
def adif(src,dst,cpe):
    d=open(src,'rb').read(); pos=0; blocks=[]
    while pos+7<=len(d):
        ln=((d[pos+3]&3)<<11)|(d[pos+4]<<3)|(d[pos+5]>>5); blocks.append(d[pos+7:pos+ln]); pos+=ln
    sfi=(d[2]>>2)&15
    bits=''.join(format(v,'0%db'%n) for n,v in ((1,0),(1,0),(1,0),(1,1),(23,64000),(4,0),(4,0),(2,1),(4,sfi),(4,1),(4,0),(4,0),(2,0),(3,0),(4,0),(1,0),(1,0),(1,0),(1,cpe),(4,0)))
    while len(bits)%8: bits+='0'
    bits+='00000000'
    open(dst,'wb').write(b'ADIF'+int(bits,2).to_bytes(len(bits)//8,'big')+b''.join(blocks))
adif('lc.aac','lc.adif',1)
adif('lc_mono.aac','lc_mono.adif',0)
PY
# MP4/M4A (etap 2: indeksowany MP4): LC stereo/mono, HE-AAC v2 (fdkaac, z lista edycji) i fragmentowany MP4 (make_fragmented.py z lc.m4a: moov bez tablic probek + mvex/trex, moof/trun po 24 probki)
"$FDK" -p 2  -b 96000 -f 0 --no-timestamp -o lc.m4a      "$W/in.wav"      >/dev/null 2>&1
"$FDK" -p 2  -b 64000 -f 0 --no-timestamp -o lc_mono.m4a "$W/in_mono.wav" >/dev/null 2>&1
"$FDK" -p 29 -b 48000 -f 0 --no-timestamp -o he2.m4a     "$W/in.wav"      >/dev/null 2>&1
python3 -I make_fragmented.py lc.m4a lc_frag.m4a
python3 -I make_mdat_first.py lc_frag.m4a lc_frag_mdatfirst.m4a
for f in lc lc_mono he1 he2; do python3 -I make_faststart.py $f.m4a ${f}_fast.m4a; done      # moov przed mdat: wejscie nieprzewijalne (MP4 na zywo)
# 44,1 kHz (etap 2: zmiana parametrow w srodku strumienia ADTS/LOAS; testy skleja pliki o roznych parametrach)
python3 -I - "$W/in44.wav" <<'PY'
import sys,wave
import numpy as np
sr=44100; t=np.arange(sr*2)/sr; rng=np.random.default_rng(3)
x=np.zeros((len(t),2))
for c in range(2): x[:,c]=0.25*np.sin(2*np.pi*(200*1.41**c)*t+c)+0.03*rng.standard_normal(len(t))
w=wave.open(sys.argv[1],'wb'); w.setnchannels(2); w.setsampwidth(2); w.setframerate(sr); w.writeframes((np.clip(x,-1,1)*32767).astype('<i2').tobytes()); w.close()
PY
"$FDK" -p 2 -b 96000 -f 2  -o lc44.aac  "$W/in44.wav" >/dev/null 2>&1
"$FDK" -p 2 -b 96000 -f 10 --header-period 1 -o lc44.loas "$W/in44.wav" >/dev/null 2>&1
ls -l *.aac *.m4a *.loas *.adif | awk '{print $5,$9}'
# AAC-LD / AAC-ELD (error-resilient syntax, no resilience tools): 3 s synthetic stereo and mono (tones, noise, clicks; numpy default_rng(11)), fdkaac -p 23 / -p 39 in M4A.
# These exercise src/decoders/modern/aac/sauti_aac_er.c; the decoder is compared with FFmpeg by scripts/test_aac_er_vs_ffmpeg.sh.
python3 -I - "$W" <<'PY'
import sys,wave
import numpy as np
sr=48000; n=sr*3; t=np.arange(n)/sr; rng=np.random.default_rng(11)
for name,ch in (("st",2),("mono",1)):
    x=np.zeros((n,ch))
    for c in range(ch):
        x[:,c]=0.22*np.sin(2*np.pi*(220*1.33**c)*t)+0.12*np.sin(2*np.pi*(1700+300*c)*t*(1+0.1*np.sin(2*np.pi*0.5*t)))+0.02*rng.standard_normal(n)
    for k in range(0,n,9000): x[k:k+40,:]+=0.5
    w=wave.open(sys.argv[1]+"/er_"+name+".wav",'wb'); w.setnchannels(ch); w.setsampwidth(2); w.setframerate(sr); w.writeframes((np.clip(x,-1,1)*32767).astype('<i2').tobytes()); w.close()
PY
"$FDK" -p 23 -b 64000 -f 0 --no-timestamp -o ld_st.m4a   "$W/er_st.wav"   >/dev/null 2>&1
"$FDK" -p 39 -b 64000 -f 0 --no-timestamp -o eld_st.m4a  "$W/er_st.wav"   >/dev/null 2>&1
"$FDK" -p 23 -b 32000 -f 0 --no-timestamp -o ld_mono.m4a "$W/er_mono.wav" >/dev/null 2>&1
"$FDK" -p 39 -b 32000 -f 0 --no-timestamp -o eld_mono.m4a "$W/er_mono.wav" >/dev/null 2>&1
# high-bitrate AAC-LD / AAC-ELD (192 kb/s, broadband noise up to 24 kHz): the encoder then codes the upper half of the spectrum, which the 64 kb/s fixtures leave empty
python3 -I - "$W" <<'PY'
import sys,wave
import numpy as np
sr=48000; n=sr; t=np.arange(n)/sr; rng=np.random.default_rng(23)
x=np.zeros((n,2))
for c in range(2): x[:,c]=0.15*np.sin(2*np.pi*(330*1.21**c)*t)+0.12*rng.standard_normal(n)*(0.4+0.6*np.sin(2*np.pi*3*t)**2)
w=wave.open(sys.argv[1]+"/er_hq.wav",'wb'); w.setnchannels(2); w.setsampwidth(2); w.setframerate(sr); w.writeframes((np.clip(x,-1,1)*32767).astype('<i2').tobytes()); w.close()
PY
"$FDK" -p 39 -b 192000 -f 0 --no-timestamp -o eld_hq.m4a "$W/er_hq.wav" >/dev/null 2>&1
"$FDK" -p 23 -b 192000 -f 0 --no-timestamp -o ld_hq.m4a  "$W/er_hq.wav" >/dev/null 2>&1
# AAC-LD / AAC-ELD in LOAS/LATM (fdkaac -f 10, StreamMuxConfig repeated in every element)
"$FDK" -p 23 -b 64000 -f 10 --header-period 1 -o ld_st.loas  "$W/er_st.wav" >/dev/null 2>&1
"$FDK" -p 39 -b 64000 -f 10 --header-period 1 -o eld_st.loas "$W/er_st.wav" >/dev/null 2>&1
