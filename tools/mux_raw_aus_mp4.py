#!/usr/bin/env python3
"""Test tool (not a product): wraps an AudioSpecificConfig and raw access units (the .asc / .aus pair of tools/sauti_aac_gen.c and tools/mp4_dump_aus.py) into a minimal MP4 (M4A) file so that
FFmpeg, FDK and Sauti can open it.  usage: mux_raw_aus_mp4.py PREFIX OUT.m4a [FRAME_LENGTH]   (frame length = samples per access unit and channel, default taken from the ASC: 1024, or 960 with
frameLengthFlag = 1; pass it for AAC-LD / ELD)."""
import struct, sys

RATES = [96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350]
def box(t, *p): d = b''.join(p); return struct.pack('>I4s', 8 + len(d), t) + d
def full(t, ver, flags, *p): return box(t, struct.pack('>I', (ver << 24) | flags), *p)
def desc(tag, d): return bytes([tag]) + bytes([0x80 | (len(d) >> 21 & 127), 0x80 | (len(d) >> 14 & 127), 0x80 | (len(d) >> 7 & 127), len(d) & 127]) + d

def main():
    prefix, out = sys.argv[1], sys.argv[2]
    asc = open(prefix + '.asc', 'rb').read(); raw = open(prefix + '.aus', 'rb').read()
    aus = []; p = 0
    while p + 4 <= len(raw):
        n = struct.unpack('>I', raw[p:p+4])[0]; aus.append(raw[p+4:p+4+n]); p += 4 + n
    bits = ''.join(format(b, '08b') for b in asc)
    aot = int(bits[:5], 2); pos = 5
    if aot == 31: aot = 32 + int(bits[5:11], 2); pos = 11
    sfi = int(bits[pos:pos+4], 2); pos += 4; ch = int(bits[pos:pos+4], 2); pos += 4
    rate = RATES[sfi]
    frame = int(sys.argv[3]) if len(sys.argv) > 3 else (960 if aot == 2 and bits[pos] == '1' else 1024)
    esds = full(b'esds', 0, 0, desc(3, struct.pack('>HB', 1, 0) + desc(4, struct.pack('>BB', 0x40, 0x15) + b'\0\0\0' + struct.pack('>II', 0, 0) + desc(5, asc)) + desc(6, b'\x02')))
    mp4a = box(b'mp4a', b'\0' * 6, struct.pack('>H', 1), struct.pack('>HHI', 0, 0, 0), struct.pack('>HHHH', ch, 16, 0, 0), struct.pack('>I', (rate << 16) if rate < 65536 else 0), esds)
    stsd = full(b'stsd', 0, 0, struct.pack('>I', 1), mp4a)
    stts = full(b'stts', 0, 0, struct.pack('>III', 1, len(aus), frame))
    stsc = full(b'stsc', 0, 0, struct.pack('>IIII', 1, 1, len(aus), 1))
    stsz = full(b'stsz', 0, 0, struct.pack('>II', 0, len(aus)), b''.join(struct.pack('>I', len(a)) for a in aus))
    ftyp = box(b'ftyp', b'M4A ', struct.pack('>I', 0), b'M4A mp42isom')
    # chunk offset depends on the final moov size: build twice
    def build(off):
        stco = full(b'stco', 0, 0, struct.pack('>II', 1, off))
        stbl = box(b'stbl', stsd, stts, stsc, stsz, stco)
        minf = box(b'minf', full(b'smhd', 0, 0, struct.pack('>HH', 0, 0)), box(b'dinf', full(b'dref', 0, 0, struct.pack('>I', 1), full(b'url ', 0, 1))), stbl)
        mdia = box(b'mdia', full(b'mdhd', 0, 0, struct.pack('>IIIIHH', 0, 0, rate, len(aus) * frame, 0x55c4, 0)), full(b'hdlr', 0, 0, struct.pack('>I4s', 0, b'soun'), b'\0' * 12, b'SoundHandler\0'), minf)
        trak = box(b'trak', full(b'tkhd', 0, 7, struct.pack('>IIIII', 0, 0, 1, 0, len(aus) * frame), b'\0' * 8, struct.pack('>HHHH', 0, 0, 0x0100, 0), struct.pack('>9I', 0x10000, 0, 0, 0, 0x10000, 0, 0, 0, 0x40000000), struct.pack('>II', 0, 0)), mdia)
        mvhd = full(b'mvhd', 0, 0, struct.pack('>IIII', 0, 0, rate, len(aus) * frame), struct.pack('>IH', 0x10000, 0x100), b'\0' * 10, struct.pack('>9I', 0x10000, 0, 0, 0, 0x10000, 0, 0, 0, 0x40000000), b'\0' * 24, struct.pack('>I', 2))
        return box(b'moov', mvhd, trak)
    moov = build(0); off = len(ftyp) + len(moov) + 8
    moov = build(off)
    open(out, 'wb').write(ftyp + moov + struct.pack('>I4s', 8 + sum(len(a) for a in aus), b'mdat') + b''.join(aus))
main()
