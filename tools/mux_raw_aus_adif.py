#!/usr/bin/env python3
"""Test tool (not a product): wraps the raw access units of tools/sauti_aac_gen.c (PREFIX.asc / PREFIX.aus) in an ADIF stream: the 'ADIF' header (variable bit rate, no copyright id), one
program_config_element (profile 0 Main / 1 LC / 3 LTP from the AudioSpecificConfig's object type 1 / 2 / 4, one front element: SCE or CPE), then the raw_data_blocks one after another.
usage: mux_raw_aus_adif.py PREFIX OUT.adif"""
import struct, sys

def main():
    prefix, out = sys.argv[1], sys.argv[2]
    asc = open(prefix + '.asc', 'rb').read(); raw = open(prefix + '.aus', 'rb').read()
    bits = ''.join(format(b, '08b') for b in asc)
    aot = int(bits[:5], 2); sfi = int(bits[5:9], 2); ch = int(bits[9:13], 2)
    profile = {1: 0, 2: 1, 4: 3}[aot]
    if ch not in (1, 2): raise SystemExit("one front element only: mono or stereo")
    b = ''
    b += '0'                                   # copyright_id_present
    b += '00'                                  # original_copy, home
    b += '1'                                   # bitstream_type: variable
    b += format(0, '023b')                     # bitrate
    b += format(0, '04b')                      # num_program_config_elements - 1
    b += format(0, '04b')                      # element_instance_tag
    b += format(profile, '02b') + format(sfi, '04b')
    b += format(1, '04b') + format(0, '04b') + format(0, '04b') + format(0, '02b') + format(0, '03b') + format(0, '04b')    # one front element, no side / back / lfe / assoc / cc
    b += '000'                                 # mixdown flags
    b += ('1' if ch == 2 else '0') + format(0, '04b')                                          # front element: is_cpe, tag
    b += '0' * (-len(b) % 8)                   # byte_alignment (the header starts at bit 32, aligned)
    b += format(0, '08b')                      # comment_field_bytes
    hdr = b'ADIF' + int(b, 2).to_bytes(len(b) // 8, 'big')
    p = 0; blocks = b''
    while p + 4 <= len(raw):
        n = struct.unpack('>I', raw[p:p+4])[0]; blocks += raw[p+4:p+4+n]; p += 4 + n
    open(out, 'wb').write(hdr + blocks)

main()
