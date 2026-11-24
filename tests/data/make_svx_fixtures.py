#!/usr/bin/env python3
"""Regenerates the Amiga 8SVX fixtures svx_*.8svx and their expected PCM svx_*.pcm (16-bit little-endian, interleaved) with FFmpeg (8svx_fib / 8svx_exp / pcm_s8 decoders, the independent reference):
mono Fibonacci delta (long enough for the decoder's seek checkpoints), exponential delta, stereo Fibonacci delta, stereo PCM with the CHAN chunk after the data.  The compressed bodies are random code
streams (every code, saturation at both ends).  usage: tests/data/make_svx_fixtures.py   (needs ffmpeg; writes next to the script)"""
import os, random, struct, subprocess
here = os.path.dirname(os.path.abspath(__file__))
def ck(tag, data): return tag + struct.pack('>I', len(data)) + data + (b'\0' if len(data) & 1 else b'')
def form(chunks): return b'FORM' + struct.pack('>I', 4 + sum(len(c) for c in chunks)) + b'8SVX' + b''.join(chunks)
def vhdr(per, comp, rate): return ck(b'VHDR', struct.pack('>IIIHBBI', per, 0, 32, rate, 1, comp, 65536))
rng = random.Random(1985)
def codes(n, hold=0):                                   # random codes; `hold` = a stretch of extreme codes that saturates the value
    b = bytearray(rng.randrange(256) for _ in range(n))
    for i in range(hold): b[n // 3 + i] = 0xFF if i < hold // 2 else 0x00
    return bytes(b)
files = {}
files['svx_fib_mono'] = form([vhdr(9000, 1, 22050), ck(b'NAME', b'fibonacci mono'), ck(b'BODY', bytes([0, 0x30]) + codes(9000, 200))])
files['svx_exp_mono'] = form([vhdr(3000, 2, 11025), ck(b'BODY', bytes([0, 0xF0]) + codes(3000, 40))])
files['svx_fib_stereo'] = form([vhdr(6000, 1, 44100), ck(b'CHAN', struct.pack('>I', 6)), ck(b'BODY', bytes([0, 0x10]) + codes(6000, 100) + bytes([0, 0xE0]) + codes(6000, 100))])
pl = bytes((i * 3) & 255 for i in range(5001)); pr = bytes((255 - i * 5) & 255 for i in range(5001))
files['svx_pcm_stereo'] = form([vhdr(5001, 0, 8363), ck(b'BODY', pl + pr), ck(b'CHAN', struct.pack('>I', 6))])
for name, data in files.items():
    open(os.path.join(here, name + '.8svx'), 'wb').write(data)
    out = subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', os.path.join(here, name + '.8svx'), '-f', 's16le', '-acodec', 'pcm_s16le', '-'], capture_output=True, check=True).stdout
    open(os.path.join(here, name + '.pcm'), 'wb').write(out)
    print(name, len(data), len(out) // 2)
