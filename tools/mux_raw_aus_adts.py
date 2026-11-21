#!/usr/bin/env python3
"""Test tool (not a product): wraps the raw access units of tools/sauti_aac_gen.c (PREFIX.asc / PREFIX.aus) in ADTS, one header per access unit, the profile field taken from the
AudioSpecificConfig's object type (1 = Main -> profile 0, 2 = LC -> profile 1, 4 = LTP -> profile 3).
usage: mux_raw_aus_adts.py PREFIX OUT.aac [--crc] [--crc-corrupt N]   (frames of 1024 lines only: ADTS cannot carry 960)
  --crc          protection_absent = 0 and the 16-bit error check after the header: CRC-16 (x^16+x^15+x^2+1, init 0xffff, MSB first) over the 56 header bits, the first 192 bits of the channel
                 element after its id and, for a CPE, the first 128 bits of the second channel (zero padded when shorter); PREFIX.crcinfo (from the generator) gives the second channel's bit offset
  --crc-corrupt N  with --crc: write a wrong CRC in access unit N (for tests of the check)."""
import struct, sys

def crc16(bits, crc=0xffff):
    for b in bits:
        top = (crc >> 15) & 1; crc = (crc << 1) & 0xffff
        if top ^ b: crc ^= 0x8005
    return crc

def bits_of(data, start, count):
    return [(data[(start + i) >> 3] >> (7 - ((start + i) & 7))) & 1 for i in range(count)]

def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    want_crc = '--crc' in sys.argv; corrupt = None
    if '--crc-corrupt' in sys.argv: corrupt = int(sys.argv[sys.argv.index('--crc-corrupt') + 1]); args = [a for a in args if a != str(corrupt)]
    prefix, out = args[0], args[1]
    asc = open(prefix + '.asc', 'rb').read(); raw = open(prefix + '.aus', 'rb').read()
    info = open(prefix + '.crcinfo', 'rb').read() if want_crc else b''
    bits = ''.join(format(b, '08b') for b in asc)
    aot = int(bits[:5], 2); sfi = int(bits[5:9], 2); ch = int(bits[9:13], 2)
    if aot not in (1, 2, 4): raise SystemExit("only AOT 1 (Main), 2 (LC) and 4 (LTP)")
    p = 0; res = b''; idx = 0
    while p + 4 <= len(raw):
        n = struct.unpack('>I', raw[p:p+4])[0]; au = raw[p+4:p+4+n]; p += 4 + n
        l = 7 + (2 if want_crc else 0) + len(au)
        h = bytearray(7)
        h[0] = 0xff; h[1] = 0xf0 if want_crc else 0xf1                    # syncword, MPEG-4, layer 0, protection_absent
        h[2] = ((aot - 1) << 6) | (sfi << 2) | ((ch >> 2) & 1)
        h[3] = ((ch & 3) << 6) | ((l >> 11) & 3)
        h[4] = (l >> 3) & 0xff
        h[5] = ((l & 7) << 5) | 0x1f
        h[6] = 0xfc
        crc = b''
        if want_crc:
            total = ''.join(format(b, '08b') for b in au); last = total.rindex('1')      # the last set bit ends ID_END (111); the rest is byte alignment
            end = last - 2                                                  # the channel element is [0, end)
            first = min(end - 3, 192)
            c = crc16(bits_of(bytes(h), 0, 56))
            c = crc16(bits_of(au, 3, first), c); c = crc16([0] * (192 - first), c)
            second_start = struct.unpack('>I', info[4 * idx:4 * idx + 4])[0]
            if second_start != 0xffffffff:
                second = min(end - second_start, 128)
                c = crc16(bits_of(au, second_start, second), c); c = crc16([0] * (128 - second), c)
            if corrupt is not None and idx == corrupt: c ^= 0x0101
            crc = struct.pack('>H', c)
        res += bytes(h) + crc + au; idx += 1
    open(out, 'wb').write(res)

main()
