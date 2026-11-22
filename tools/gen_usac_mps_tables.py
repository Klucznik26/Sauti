#!/usr/bin/env python3
"""Generates src/decoders/modern/aac/sauti_usac_mps_tables.c and include/sauti_usac_mps_tables.h: the DATA tables of the MPEG Surround 212 parameter decoder of USAC (ISO/IEC 23003-3 clause 7.? / 23003-1): the
Huffman node tables of CLD, ICC and IPD (1-D, 2-D by lav, pilot / first value), the table of the lav index, the hybrid-band to parameter-band maps, the parameter dequantisation tables, the downmix gain table, read from
libxaac (https://github.com/ittiam-systems/libxaac, Apache License 2.0, Copyright (C) 2018 The Android Open Source Project, originally developed and contributed by Ittiam Systems Pvt. Ltd) - data only, the decoder logic is
written for Sauti.  Provenance of every table is in the generated header (and in docs/BIBLIOTEKA_PLAN.md); the licence text and NOTICE of libxaac are in third_party/libxaac/.
usage: tools/gen_usac_mps_tables.py [LIBXAAC_DIR]   (default ~/Sauti-tools/libxaac; read-only, the checkout stays outside the repository)"""
import os, re, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
XAAC = os.path.expanduser(sys.argv[1] if len(sys.argv) > 1 else '~/Sauti-tools/libxaac')

def read_values(path, name):
    src = open(path, encoding='utf-8', errors='replace').read()
    m = re.search(r'\b' + re.escape(name) + r'\s*(\[[^=;]*\])*\s*=\s*\{', src)
    if not m: raise SystemExit('table %s not found in %s' % (name, path))
    i = m.end(); depth = 1; j = i
    while depth:
        c = src[j]; depth += (c == '{') - (c == '}'); j += 1
    body = re.sub(r'//[^\n]*|/\*.*?\*/', '', src[i:j - 1], flags=re.S).replace('{', ' ').replace('}', ' ')
    vals = []
    for item in body.split(','):
        item = item.strip()
        if not item: continue
        item = re.sub(r'(?<=[0-9.])[fF]\b', '', item)
        if re.fullmatch(r'[-+0-9.eE/ ]+', item): vals.append(eval(item))
        else: raise SystemExit('%s: cannot read %r' % (name, item))
    return vals

ROM = os.path.join(XAAC, 'decoder', 'ixheaacd_mps_rom.c')
PRS = os.path.join(XAAC, 'decoder', 'ixheaacd_mps_parse.c')
tables = []        # (new name, C type, dims, values, meaning, libxaac name)
def add(new, c, dims, vals, doc, old):
    n = 1
    for d in dims: n *= d
    if len(vals) != n: raise SystemExit('%s: %d values, expected %d' % (new, len(vals), n))
    tables.append((new, c, dims, vals, doc, old))

def split_huff(name, one_d, lavs, hasp0=False):
    """flat node values of ia_huff_{cld,icc,ipd}_nodes_struct: [hp0] h_1_dim[3] of one_d pairs, h_2_dim[3][2] of lav tables"""
    v = read_values(ROM, name); pos = 0
    out = {}
    if hasp0: out['hp0'] = v[pos:pos + 14]; pos += 14
    out['one'] = [v[pos + k * 2 * one_d:pos + (k + 1) * 2 * one_d] for k in range(3)]; pos += 3 * 2 * one_d
    two = [[None, None] for _ in range(3)]
    for t in range(3):
        for p in range(2):
            d = {}
            for lav, nodes in lavs:
                d[lav] = v[pos:pos + 2 * nodes]; pos += 2 * nodes
            two[t][p] = d
    out['two'] = two
    if pos != len(v): raise SystemExit('%s: %d values left over' % (name, len(v) - pos))
    return out

cld = split_huff('ixheaacd_huff_cld_nodes', 30, [(3, 15), (5, 35), (7, 63), (9, 99)])
icc = split_huff('ixheaacd_huff_icc_nodes', 7, [(1, 3), (3, 15), (5, 35), (7, 63)])
ipd = split_huff('ixheaacd_huff_ipd_nodes', 7, [(1, 3), (3, 15), (5, 35), (7, 63)], hasp0=True)
# only difference types 0 (frequency) and 1 (time) are used without pilot coding
add('sauti_usac_mps_cld_1d', 'int16_t', (2, 30, 2), cld['one'][0] + cld['one'][1], 'CLD 1-D Huffman nodes [diff type][node][bit]', 'ixheaacd_huff_cld_nodes.h_1_dim')
add('sauti_usac_mps_icc_1d', 'int16_t', (2, 7, 2), icc['one'][0] + icc['one'][1], 'ICC 1-D Huffman nodes', 'ixheaacd_huff_icc_nodes.h_1_dim')
add('sauti_usac_mps_ipd_1d', 'int16_t', (2, 7, 2), ipd['one'][0] + ipd['one'][1], 'IPD 1-D Huffman nodes', 'ixheaacd_huff_ipd_nodes.h_1_dim')
for nm, d, lavs in (('cld', cld, (3, 5, 7, 9)), ('icc', icc, (1, 3, 5, 7)), ('ipd', ipd, (1, 3, 5, 7))):
    for lav in lavs:
        nodes = {1: 3, 3: 15, 5: 35, 7: 63, 9: 99}[lav]
        flat = []
        for t in range(2):
            for p in range(2): flat += d['two'][t][p][lav]
        add('sauti_usac_mps_%s_2d_lav%d' % (nm, lav), 'int16_t', (2, 2, nodes, 2), flat, '%s 2-D Huffman nodes for lav %d [diff type][pair type][node][bit]' % (nm.upper(), lav), 'ixheaacd_huff_%s_nodes.h_2_dim.lav%d' % (nm, lav))
add('sauti_usac_mps_ipd_hp0', 'int16_t', (7, 2), ipd['hp0'], 'IPD first value Huffman nodes', 'ixheaacd_huff_ipd_nodes.hp0')
p0 = read_values(ROM, 'ixheaacd_huff_part0_nodes')
add('sauti_usac_mps_cld_p0', 'int16_t', (30, 2), p0[:60], 'CLD first value (part 0) Huffman nodes', 'ixheaacd_huff_part0_nodes.cld')
add('sauti_usac_mps_icc_p0', 'int16_t', (7, 2), p0[60:74], 'ICC first value (part 0) Huffman nodes', 'ixheaacd_huff_part0_nodes.icc')
add('sauti_usac_mps_lav_idx', 'int16_t', (3, 2), read_values(ROM, 'ixheaacd_huff_lav_idx_nodes'), 'Huffman nodes of the lav index', 'ixheaacd_huff_lav_idx_nodes')
add('sauti_usac_mps_reshape', 'int16_t', (39, 2), read_values(ROM, 'ixheaacd_huff_reshape_nodes'), 'Huffman nodes of the GES envelope run lengths', 'ixheaacd_huff_reshape_nodes')
for bands in (28, 20, 14, 10, 7, 5, 4):
    add('sauti_usac_mps_band_map_%d' % bands, 'uint8_t', (71,), read_values(PRS, 'ixheaacd_hybrid_band_71_to_processing_band_%d_map' % bands), 'parameter band of every hybrid band (71 of them) for %d parameter bands' % bands, 'ixheaacd_hybrid_band_71_to_processing_band_%d_map' % bands)
add('sauti_usac_mps_cld_dequant', 'float', (31,), read_values(PRS, 'ixheaacd_cld_de_quant_table'), 'CLD in dB by index + 15', 'ixheaacd_cld_de_quant_table')
add('sauti_usac_mps_icc_dequant', 'float', (8,), read_values(PRS, 'ixheaacd_icc_de_quant_table'), 'ICC by index', 'ixheaacd_icc_de_quant_table')
add('sauti_usac_mps_ipd_dequant', 'float', (16,), read_values(PRS, 'ixheaacd_ipd_de_quant_table'), 'IPD in radians by index', 'ixheaacd_ipd_de_quant_table')
add('sauti_usac_mps_clip_gain', 'float', (8,), read_values(PRS, 'ixheaacd_mps_clip_gain_table'), 'input gain of the downmix by bsFixedGainDMX', 'ixheaacd_mps_clip_gain_table')

rev = subprocess.run(['git', '-C', XAAC, 'rev-parse', 'HEAD'], capture_output=True, text=True).stdout.strip()
hdr = ['#ifndef SAUTI_USAC_MPS_TABLES_H', '#define SAUTI_USAC_MPS_TABLES_H', '#include <stdint.h>',
       '/* Data tables of the MPEG Surround 212 parameter decoder of USAC.  GENERATED by tools/gen_usac_mps_tables.py - do not edit.', ' * Source of the data: libxaac (ittiam-systems/libxaac, commit %s), Apache License 2.0, Copyright (C) 2018 The Android Open Source Project,' % rev,
       ' * originally developed and contributed by Ittiam Systems Pvt. Ltd; licence text and NOTICE in third_party/libxaac/.  Only the numbers are taken from there; the decoder logic is written for Sauti.', ' * Provenance register: docs/BIBLIOTEKA_PLAN.md ("Rejestr tablic MPS212 z libxaac"). */']
src = ['/* GENERATED by tools/gen_usac_mps_tables.py from libxaac (commit %s, Apache License 2.0, third_party/libxaac/LICENSE) - do not edit.  See include/sauti_usac_mps_tables.h. */' % rev, '#include "sauti_usac_mps_tables.h"', '']
def tok(v, c):
    if c != 'float': return str(int(v))
    t = '%.9g' % v
    return (t if ('.' in t or 'e' in t) else t + '.0') + 'f'
def emit(lines, vals, dims, c, indent=1):
    """nested braces, one innermost row per line"""
    pad = '    ' * indent
    if len(dims) == 1:
        toks = [tok(v, c) for v in vals]
        for k in range(0, len(toks), 12): lines.append(pad + ', '.join(toks[k:k + 12]) + ',')
        return
    step = len(vals) // dims[0]
    for k in range(dims[0]):
        lines.append(pad + '{')
        emit(lines, vals[k * step:(k + 1) * step], dims[1:], c, indent + 1)
        lines.append(pad + '},')
for new, c, dims, vals, doc, old in tables:
    dd = ''.join('[%d]' % d for d in dims)
    hdr.append('extern const %s %s%s;   /* %s (libxaac: %s) */' % (c, new, dd, doc, old))
    src.append('const %s %s%s = {' % (c, new, dd)); emit(src, vals, dims, c); src.append('};')
hdr.append('#endif')
open(os.path.join(ROOT, 'include/sauti_usac_mps_tables.h'), 'w').write('\n'.join(hdr) + '\n')
open(os.path.join(ROOT, 'src/decoders/modern/aac/sauti_usac_mps_tables.c'), 'w').write('\n'.join(src) + '\n')
print('%d tables, libxaac %s' % (len(tables), rev))
