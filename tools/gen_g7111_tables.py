#!/usr/bin/env python3
"""Generates src/decoders/modern/maktabasauti_g7111_tables.c: the two higher-band shape codebooks of ITU-T G.711.1 (2 x 32 code vectors x 6 values, Q12).

The numbers exist only in the electronic attachment of Rec. ITU-T G.711.1 (the ANSI C code, which the Recommendation calls an integral part of it; the text gives the structure of the
quantizer but not the codebooks).  This script copies ONLY the numeric data (no code) into a data file, as decided by the owner of the project on 2026-11-25; the provenance is in the
header of the generated file and in docs/G7111.md.  Nothing else of the ITU software is used or copied (the decoder is original).
usage: tools/gen_g7111_tables.py PATH/TO/G.711.1_MB-Fix_v2.00/src/mainlib/higher_band/table_highband.c > src/decoders/modern/maktabasauti_g7111_tables.c"""
import re, sys
t = open(sys.argv[1], encoding='latin-1').read()
def block(name):
    i = t.index(name); j = t.index('};', i)
    nums = [int(x) for x in re.findall(r'-?\d+', t[t.index('{', i):j])]
    assert len(nums) == 32 * 6, (name, len(nums))
    return nums
out = ['''/* MaktabaSauti, modul maktabasauti_g7111: DANE, nie kod.  Dwie ksiazki kodowe ksztaltu gornego pasma ITU-T G.711.1 (32 wektory po 6 wartosci, Q12, juz z wagami czestotliwosciowymi).
 * Pochodzenie: zalacznik elektroniczny do Rekomendacji ITU-T G.711.1 (kod ANSI C, tablice gsCodebook_0ch i gsCodebook_1ch); przepisano wylacznie liczby, skryptem tools/gen_g7111_tables.py,
 * za zgoda wlasciciela projektu z 2026-11-25.  Kod ITU nie jest uzywany ani kopiowany (wpis w rejestrze tablic: docs/G7111.md). */
#include <stdint.h>
''']
for cname, src in (('g7111_codebook0', 'gsCodebook_0ch'), ('g7111_codebook1', 'gsCodebook_1ch')):
    nums = block(src)
    out.append('const int16_t %s[32][6] = {\n' % cname)
    for i in range(32): out.append('    { %s },\n' % ', '.join('%d' % v for v in nums[6 * i: 6 * i + 6]))
    out.append('};\n')
sys.stdout.write(''.join(out))
