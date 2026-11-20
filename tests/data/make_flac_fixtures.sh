#!/usr/bin/env bash
# Regenerates the small synthetic FLAC fixtures used by tests/test_maktabasauti_flac_stream.c: deterministic tones plus low noise (numpy default_rng(7)), NOT user material.
# FFmpeg's native FLAC encoder is used only here, once; the repository carries its OUTPUT, so CI needs no encoder.
# usage: tests/data/make_flac_fixtures.sh   (needs ffmpeg and python3 with numpy)
set -euo pipefail
cd "$(dirname "$0")"
FF="${SAUTI_FFMPEG:-ffmpeg}"
W="$(mktemp -d "${TMPDIR:-/tmp}/sauti-flac.XXXXXX")"; trap 'rm -rf -- "${W:?}"' EXIT
python3 -I - "$W" <<'PY'
import sys,wave,struct
import numpy as np
w=sys.argv[1]; rng=np.random.default_rng(7)
def sig(n,ch,sr):
    t=np.arange(n)/sr; x=np.zeros((n,ch))
    for c in range(ch): x[:,c]=0.3*np.sin(2*np.pi*(220*1.26**c)*t+c)+0.0008*rng.standard_normal(n)
    return x
def wav(path,x,sr,width):
    f=wave.open(path,'wb'); f.setnchannels(x.shape[1]); f.setsampwidth(width); f.setframerate(sr)
    if width==2: f.writeframes((np.clip(x,-1,1)*32767).astype('<i2').tobytes())
    else:
        v=(np.clip(x,-1,1)*8388607).astype('<i4').reshape(-1); f.writeframes(b''.join(struct.pack('<i',a)[:3] for a in v))
    f.close()
wav(w+'/st16.wav',sig(30000,2,44100),44100,2)
wav(w+'/mono16.wav',sig(20000,1,32000),32000,2)
wav(w+'/st24.wav',sig(30000,2,48000),48000,3)
# okladka: szum 160x160 jako PNG (>16 KiB w bloku PICTURE, wiec naglowek wykracza poza okno czytnika)
import zlib
img=(rng.integers(0,256,(100,100,3))).astype('u1')
raw=b''.join(b'\x00'+img[r].tobytes() for r in range(100))
def ch(t,d): c=struct.pack('>I',len(d))+t+d; return c+struct.pack('>I',zlib.crc32(t+d)&0xffffffff)
open(w+'/cover.png','wb').write(b'\x89PNG\r\n\x1a\n'+ch(b'IHDR',struct.pack('>IIBBBBB',100,100,8,2,0,0,0))+ch(b'IDAT',zlib.compress(raw,0))+ch(b'IEND',b''))
PY
E() { "$FF" -v error -y "$@"; }
E -i "$W/st16.wav"    -c:a flac -compression_level 5 -frame_size 4096 flac_st16.flac
E -i "$W/st16.wav"    -c:a flac -compression_level 8 -frame_size 1152 flac_st16_small.flac
E -i "$W/st16.wav"    -c:a flac -compression_level 0 -frame_size 16384 flac_st16_big.flac
E -i "$W/mono16.wav"  -c:a flac -compression_level 5 flac_mono16.flac
E -i "$W/st24.wav"    -c:a flac -compression_level 6 -sample_fmt s32 -frame_size 4608 flac_st24.flac
# zapis strumieniowy: wyjscie nieprzewijalne => STREAMINFO bez dlugosci (total_samples = 0)
E -i "$W/st16.wav"    -c:a flac -compression_level 5 -frame_size 4096 -f flac - > flac_st16_streamed.flac
# metadane: komentarze Vorbis + okladka (blok PICTURE > 16 KiB)
E -i "$W/st16.wav" -i "$W/cover.png" -map 0:a -map 1:v -c:a flac -c:v copy -disposition:v attached_pic -metadata title="Sauti synthetic fixture" -metadata artist="none" flac_st16_meta.flac
ls -l flac_*.flac
