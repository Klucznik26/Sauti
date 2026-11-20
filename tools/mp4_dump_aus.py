#!/usr/bin/env python3
"""Test tool (not a product): extracts the AudioSpecificConfig and the access units of the first audio track of an MP4/M4A file.
usage: mp4_dump_aus.py IN.m4a OUT_PREFIX   ->  OUT_PREFIX.asc (raw ASC bytes) and OUT_PREFIX.aus (per access unit: 4-byte big-endian length + bytes).
Used to feed access units that cannot be wrapped in ADTS (AAC-LD / ELD) to oracle decoders and to tools/sauti_er_cli.c."""
import struct, sys

def boxes(d, start, end):
    p = start
    while p + 8 <= end:
        size, typ = struct.unpack('>I4s', d[p:p+8]); hdr = 8
        if size == 1: size = struct.unpack('>Q', d[p+8:p+16])[0]; hdr = 16
        elif size == 0: size = end - p
        if size < hdr or p + size > end: break
        yield typ.decode('latin1'), p + hdr, p + size
        p += size

def find(d, start, end, path):
    for typ, s, e in boxes(d, start, end):
        if typ == path[0]:
            if len(path) == 1: return s, e
            r = find(d, s, e, path[1:])
            if r: return r
    return None

def desc_len(d, p):
    n = 0
    for _ in range(4):
        b = d[p]; p += 1; n = (n << 7) | (b & 0x7f)
        if not b & 0x80: break
    return n, p

def main():
    d = open(sys.argv[1], 'rb').read()
    moov = find(d, 0, len(d), ['moov']); assert moov
    for typ, s, e in boxes(d, *moov):
        if typ != 'trak': continue
        hdlr = find(d, s, e, ['mdia', 'hdlr'])
        if not hdlr or d[hdlr[0] + 8:hdlr[0] + 12] != b'soun': continue
        stbl = find(d, s, e, ['mdia', 'minf', 'stbl']); stsd = find(d, *stbl, ['stsd']) if False else None
        sb, se = stbl
        stsd = find(d, sb, se, ['stsd'])
        entry = stsd[0] + 8                      # skip version/flags + entry_count
        esize = struct.unpack('>I', d[entry:entry+4])[0]
        asc = None
        # AudioSampleEntry: 8 byte box header + 28 bytes (v0); child boxes follow (QuickTime v1/v2 entries are longer: handled by scanning for esds)
        k = d.find(b'esds', entry, entry + esize)
        assert k > 0, 'no esds'
        p = k + 4 + 4                             # type + version/flags
        assert d[p] == 3; ln, p = desc_len(d, p + 1); p += 3
        assert d[p] == 4; ln, p = desc_len(d, p + 1); p += 13
        assert d[p] == 5; ln, p = desc_len(d, p + 1); asc = d[p:p+ln]
        sizes = []
        stsz = find(d, sb, se, ['stsz']); fixed, cnt = struct.unpack('>II', d[stsz[0]+4:stsz[0]+12])
        sizes = [fixed] * cnt if fixed else list(struct.unpack('>%dI' % cnt, d[stsz[0]+12:stsz[0]+12+4*cnt]))
        stsc = find(d, sb, se, ['stsc']); n = struct.unpack('>I', d[stsc[0]+4:stsc[0]+8])[0]
        runs = [struct.unpack('>III', d[stsc[0]+8+12*i:stsc[0]+20+12*i]) for i in range(n)]
        stco = find(d, sb, se, ['stco']); wide = False
        if not stco: stco = find(d, sb, se, ['co64']); wide = True
        m = struct.unpack('>I', d[stco[0]+4:stco[0]+8])[0]
        offs = list(struct.unpack('>%d%s' % (m, 'Q' if wide else 'I'), d[stco[0]+8:stco[0]+8+(8 if wide else 4)*m]))
        out = bytearray(); si = 0
        for ci in range(m):
            per = 0
            for first, spc, _ in runs:
                if ci + 1 >= first: per = spc
            o = offs[ci]
            for _ in range(per):
                if si >= len(sizes): break
                out += struct.pack('>I', sizes[si]) + d[o:o+sizes[si]]; o += sizes[si]; si += 1
        open(sys.argv[2] + '.asc', 'wb').write(asc); open(sys.argv[2] + '.aus', 'wb').write(out)
        print('ASC', asc.hex(), 'access units', si)
        return
    sys.exit('no audio track')
main()
