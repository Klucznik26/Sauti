#!/usr/bin/env python3
"""Test tool (not a product): wraps the raw access units of tools/sauti_aac_gen.c (PREFIX.asc / PREFIX.aus) in a LOAS / LATM stream (AudioSyncStream): every frame carries its StreamMuxConfig
(audioMuxVersion 0, one program, one layer, frameLengthType 0) with the AudioSpecificConfig, then the payload length and the payload.  For the object types FFmpeg's LATM muxer cannot write
(ER AAC LC / LTP, Main with 960 lines, ...).  usage: mux_raw_aus_loas.py PREFIX OUT.loas"""
import struct, sys

def main():
    prefix, out = sys.argv[1], sys.argv[2]
    asc = open(prefix + '.asc', 'rb').read(); raw = open(prefix + '.aus', 'rb').read()
    asc_bits = ''.join(format(b, '08b') for b in asc)
    # the ASC length in bits: the generator writes it byte-padded; the decoders parse it by syntax, so the padding bits must not be counted: find the end from the object type
    aot = int(asc_bits[:5], 2)
    n = 5 + 4 + 4 + 3                                           # aot, rate, config, GASpecificConfig's three flags
    if aot in (17, 19, 23): n += 3 + 1 + 2                      # ER (the generator sets extensionFlag = 1): resilience flags (3), extensionFlag3, epConfig (2)
    asc_bits = asc_bits[:n]
    p = 0; res = b''
    while p + 4 <= len(raw):
        ln = struct.unpack('>I', raw[p:p+4])[0]; au = raw[p+4:p+4+ln]; p += 4 + ln
        b = '0'                                                 # useSameStreamMux = 0: the config follows
        b += '0' + '1' + format(0, '06b') + format(0, '04b') + format(0, '03b')    # audioMuxVersion, allStreamsSameTimeFraming, numSubFrames, numProgram, numLayer
        b += asc_bits
        b += format(0, '03b') + format(0xff, '08b') + '0' + '0'                    # frameLengthType, latmBufferFullness, otherDataPresent, crcCheckPresent
        l = len(au)
        while l >= 255: b += format(255, '08b'); l -= 255
        b += format(l, '08b')                                   # PayloadLengthInfo
        b += ''.join(format(x, '08b') for x in au)
        b += '0' * (-len(b) % 8)
        body = int(b, 2).to_bytes(len(b) // 8, 'big')
        hdr = (0x2b7 << 13) | len(body)
        res += struct.pack('>H', hdr >> 8)[0:0] + bytes([(hdr >> 16) & 0xff, (hdr >> 8) & 0xff, hdr & 0xff]) + body
    open(out, 'wb').write(res)

main()
