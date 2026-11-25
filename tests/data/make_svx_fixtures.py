#!/usr/bin/env python3
"""Regenerates the Amiga 8SVX fixtures svx_*.8svx and their expected PCM svx_*.pcm (16-bit little-endian, interleaved).  PCM comes from FFmpeg (pcm_s8); the Fibonacci / exponential values come from the
model below: HIGH nibble first, value WRAPS modulo 256 (FFmpeg takes the low nibble first and clips, which does NOT match files written by an Amiga program: docs/IFF_8SVX.md).  The model is validated bit for bit
against wavepak 1.3 running on an emulated Amiga: tests/data/svx_wavepak_* are ITS files and the PCM of its own decoder (not made by this script).  Fixtures:
mono Fibonacci delta (long enough for the decoder's seek checkpoints), exponential delta, stereo Fibonacci delta, stereo PCM with the CHAN chunk after the data.  The compressed bodies are random code
streams (every code, saturation at both ends).  usage: tests/data/make_svx_fixtures.py   (needs ffmpeg; writes next to the script)"""
import os, random, struct, subprocess
FIB = [-34, -21, -13, -8, -5, -3, -2, -1, 0, 1, 2, 3, 5, 8, 13, 21]; EXP = [-128, -64, -32, -16, -8, -4, -2, -1, 0, 1, 2, 4, 8, 16, 32, 64]
def model(blocks, table):
    chans = []
    for b in blocks:
        y = b[1] - 256 if b[1] > 127 else b[1]; out = []
        for c in b[2:]:
            for nib in (c >> 4, c & 15): y = (y + table[nib] + 128) % 256 - 128; out.append(y)
        chans.append(out)
    return b''.join(struct.pack('<h', chans[k][i] * 256) for i in range(len(chans[0])) for k in range(len(chans)))
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
def blocks_of(data):                                    # the BODY of our own fixture split into the per-channel blocks (compressed: header + codes)
    at = data.index(b'BODY'); n = struct.unpack('>I', data[at + 4:at + 8])[0]; body = data[at + 8:at + 8 + n]
    return [body[:n // 2], body[n // 2:]] if b'CHAN' in data[:at] else [body]
for name, data in files.items():
    open(os.path.join(here, name + '.8svx'), 'wb').write(data)
    if name != 'svx_pcm_stereo':
        out = model(blocks_of(data), FIB if 'fib' in name else EXP)
        open(os.path.join(here, name + '.pcm'), 'wb').write(out); print(name, len(data), len(out) // 2); continue
    out = subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', os.path.join(here, name + '.8svx'), '-f', 's16le', '-acodec', 'pcm_s16le', '-'], capture_output=True, check=True).stdout
    open(os.path.join(here, name + '.pcm'), 'wb').write(out)
    print(name, len(data), len(out) // 2)
