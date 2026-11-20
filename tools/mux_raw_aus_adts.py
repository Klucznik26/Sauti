#!/usr/bin/env python3
"""Test tool (not a product): wraps the raw access units of tools/sauti_aac_gen.c (PREFIX.asc / PREFIX.aus) in ADTS, one header per access unit (no CRC), the profile field taken from the
AudioSpecificConfig's object type (1 = Main -> profile 0, 2 = LC -> profile 1).  usage: mux_raw_aus_adts.py PREFIX OUT.aac   (frames of 1024 lines only: ADTS cannot carry 960)."""
import struct, sys

def main():
    prefix, out = sys.argv[1], sys.argv[2]
    asc = open(prefix + '.asc', 'rb').read(); raw = open(prefix + '.aus', 'rb').read()
    bits = ''.join(format(b, '08b') for b in asc)
    aot = int(bits[:5], 2); sfi = int(bits[5:9], 2); ch = int(bits[9:13], 2)
    if aot not in (1, 2): raise SystemExit("only AOT 1 (Main) and 2 (LC)")
    p = 0; res = b''
    while p + 4 <= len(raw):
        n = struct.unpack('>I', raw[p:p+4])[0]; au = raw[p+4:p+4+n]; p += 4 + n
        l = 7 + len(au)
        h = bytearray(7)
        h[0] = 0xff; h[1] = 0xf1                                         # syncword, MPEG-4, layer 0, no CRC
        h[2] = ((aot - 1) << 6) | (sfi << 2) | ((ch >> 2) & 1)
        h[3] = ((ch & 3) << 6) | ((l >> 11) & 3)
        h[4] = (l >> 3) & 0xff
        h[5] = ((l & 7) << 5) | 0x1f
        h[6] = 0xfc
        res += bytes(h) + au
    open(out, 'wb').write(res)

main()
