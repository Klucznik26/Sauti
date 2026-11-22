#!/usr/bin/env python3
"""Generates src/decoders/modern/aac/sauti_usac_hbe_tables.c and include/sauti_usac_hbe_tables.h: the DATA tables of the harmonic transposer (HBE) of eSBR (ISO/IEC 23003-3 7.5.3), read from the source of
libxaac (https://github.com/ittiam-systems/libxaac, Apache License 2.0, Copyright (C) 2018 The Android Open Source Project, originally developed and contributed by Ittiam Systems Pvt. Ltd) - data only, the
logic of the transposer is written for Sauti.  Provenance of every table is in the generated header (and in docs/BIBLIOTEKA_PLAN.md); the licence text and NOTICE of libxaac are in third_party/libxaac/.
usage: tools/gen_usac_hbe_tables.py [LIBXAAC_DIR]   (default ~/Sauti-tools/libxaac; read-only, the checkout stays outside the repository)"""
import os, re, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
XAAC = os.path.expanduser(sys.argv[1] if len(sys.argv) > 1 else '~/Sauti-tools/libxaac')
SRC = 'ixheaac_esbr_rom.c'
# (name in libxaac, new name, C type, dimensions, one-line meaning)
TABLES = [
    ('ixheaac_sub_samp_qmf_window_coeff', 'sauti_usac_hbe_window', 'float', (2000,), 'sub-sampled prototype windows of the transposer filter banks: 10 * L taps for L = 4, 8, 12, 16, 20, 24, 32, 40 at offsets 0, 40, 120, 240, 400, 600, 840, 1160 (then 440 values for L = 44)'),
    ('ixheaac_start_subband2kL_tbl', 'sauti_usac_hbe_start_band_to_kl', 'int32_t', (33,), 'first low band QMF band of the transposer synthesis for every SBR start band'),
    ('ixheaac_cos_table_trans_qmf', 'sauti_usac_hbe_cos_trans_qmf', 'float', (7, 64), '(cos, sin) pairs that turn a complex QMF subband sample into the real input of the transposer synthesis (pair 16 * k_start + k)'),
    ('ixheaac_phase_vocoder_cos_table', 'sauti_usac_hbe_pv_cos', 'float', (64,), 'cosine of the per band phase rotation of the transposer output'),
    ('ixheaac_phase_vocoder_sin_table', 'sauti_usac_hbe_pv_sin', 'float', (64,), 'sine of the per band phase rotation of the transposer output'),
    ('ixheaac_hbe_post_anal_proc_interp_coeff', 'sauti_usac_hbe_interp_coeff', 'float', (4, 2), 'interpolation coefficients between neighbouring transposer columns (pitch dependent products)'),
    ('ixheaac_sel_case', 'sauti_usac_hbe_sel_case', 'float', (5, 8), 'interpolation of half band neighbours for the 3rd order product, by band parity'),
    ('ixheaac_hbe_x_prod_cos_table_trans_2', 'sauti_usac_hbe_xprod_cos_2', 'float', (512,), '(cos, sin) of the phase correction of the 2nd order cross product, by sbrPitchInBins (+ 128 for 4:1)'),
    ('ixheaac_hbe_x_prod_cos_table_trans_3', 'sauti_usac_hbe_xprod_cos_3', 'float', (512,), 'the same, 3rd order'),
    ('ixheaac_hbe_x_prod_cos_table_trans_4', 'sauti_usac_hbe_xprod_cos_4', 'float', (512,), 'the same, 4th order'),
    ('ixheaac_hbe_x_prod_cos_table_trans_4_1', 'sauti_usac_hbe_xprod_cos_4_1', 'float', (512,), 'the same, 4th order, distance 1'),
    ('ixheaac_synth_cos_table_kl_4', 'sauti_usac_hbe_synth_cos_4', 'float', (16,), 'post twiddle of the synthesis, 4 bands'),
    ('ixheaac_synth_cos_table_kl_8', 'sauti_usac_hbe_synth_cos_8', 'float', (32,), 'the same, 8 bands'),
    ('ixheaac_synth_cos_table_kl_12', 'sauti_usac_hbe_synth_cos_12', 'float', (48,), 'the same, 12 bands'),
    ('ixheaac_synth_cos_table_kl_16', 'sauti_usac_hbe_synth_cos_16', 'float', (64,), 'the same, 16 bands'),
    ('ixheaac_synth_cos_table_kl_20', 'sauti_usac_hbe_synth_cos_20', 'float', (800,), 'cosine matrix of the synthesis, 20 bands'),
    ('ixheaac_analy_cos_sin_table_kl_8', 'sauti_usac_hbe_analy_8', 'float', (32,), 'pre twiddle of the 8 band complex analysis'),
    ('ixheaac_analy_cos_sin_table_kl_16', 'sauti_usac_hbe_analy_16', 'float', (64,), 'the same, 16 bands'),
    ('ixheaac_analy_cos_sin_table_kl_24', 'sauti_usac_hbe_analy_24', 'float', (96,), 'the same, 24 bands'),
    ('ixheaac_analy_cos_sin_table_kl_32', 'sauti_usac_hbe_analy_32', 'float', (128,), 'the same, 32 bands'),
    ('ixheaac_analy_cos_sin_table_kl_40', 'sauti_usac_hbe_analy_40', 'float', (3200,), 'cosine / sine matrix of the 40 band complex analysis'),
]

def read_values(path, name):
    src = open(path, encoding='utf-8', errors='replace').read()
    m = re.search(r'\b' + re.escape(name) + r'\s*(\[[^=;]*\])+\s*=\s*\{', src)
    if not m: raise SystemExit('table %s not found in %s' % (name, path))
    i = m.end(); depth = 1; j = i
    while depth:
        c = src[j]; depth += (c == '{') - (c == '}'); j += 1
    body = re.sub(r'//[^\n]*|/\*.*?\*/', '', src[i:j - 1], flags=re.S)
    body = body.replace('{', ' ').replace('}', ' ')
    vals = []
    for item in body.split(','):
        item = item.strip()
        if not item: continue
        if not item.lower().startswith('0x'): item = re.sub(r'(?<=[0-9.])[fF]\b', '', item)
        if re.fullmatch(r'0[xX][0-9a-fA-F]+', item): vals.append(int(item, 16))
        elif re.fullmatch(r'[-+0-9.eE/ ]+', item): vals.append(eval(item))
        else: raise SystemExit('%s: cannot read %r' % (name, item))
    return vals

def main():
    rev = subprocess.run(['git', '-C', XAAC, 'rev-parse', 'HEAD'], capture_output=True, text=True).stdout.strip()
    hdr = ['#ifndef SAUTI_USAC_HBE_TABLES_H', '#define SAUTI_USAC_HBE_TABLES_H', '#include <stdint.h>',
           '/* Data tables of the harmonic transposer (HBE) of eSBR (ISO/IEC 23003-3).  GENERATED by tools/gen_usac_hbe_tables.py - do not edit.', ' * Source of the data: libxaac (ittiam-systems/libxaac, commit %s), Apache License 2.0, Copyright (C) 2018 The Android Open Source Project,' % rev,
           ' * originally developed and contributed by Ittiam Systems Pvt. Ltd; licence text and NOTICE in third_party/libxaac/.  Only the numbers are taken from there; the transposer logic is written for Sauti.', ' * Provenance register: docs/BIBLIOTEKA_PLAN.md ("Rejestr tablic LPD z libxaac"). */']
    src = ['/* GENERATED by tools/gen_usac_hbe_tables.py from libxaac (commit %s, Apache License 2.0, third_party/libxaac/LICENSE) - do not edit.  See include/sauti_usac_hbe_tables.h. */' % rev, '#include "sauti_usac_hbe_tables.h"', '']
    for old, new, c, dims, doc in TABLES:
        vals = read_values(os.path.join(XAAC, 'common', SRC), old)
        n = 1
        for d in dims: n *= d
        if old == 'ixheaac_sub_samp_qmf_window_coeff': vals = vals[:n]      # the source has the 28 / 36 band variants in a table of their own; this one is exactly 2000 values long
        if len(vals) != n: raise SystemExit('%s: %d values, expected %d' % (old, len(vals), n))
        toks = [('%.9gf' % v) if c == 'float' else str(int(v)) for v in vals]
        toks = [t if ('.' in t or 'e' in t or c != 'float') else t.replace('f', '.0f') for t in toks]
        dd = ''.join('[%d]' % d for d in dims)
        hdr.append('extern const %s %s%s;   /* %s (libxaac: %s, %s) */' % (c, new, dd, doc, old, SRC))
        src.append('const %s %s%s = {' % (c, new, dd))
        per = dims[-1] if len(dims) > 1 else 8
        multi = len(dims) > 1
        for k in range(0, n, per): src.append('    ' + ('{ ' if multi else '') + ', '.join(toks[k:k + per]) + (' },' if multi else ','))
        src.append('};')
    hdr.append('#endif')
    open(os.path.join(ROOT, 'include/sauti_usac_hbe_tables.h'), 'w').write('\n'.join(hdr) + '\n')
    open(os.path.join(ROOT, 'src/decoders/modern/aac/sauti_usac_hbe_tables.c'), 'w').write('\n'.join(src) + '\n')
    print('%d tables, libxaac %s' % (len(TABLES), rev))
main()
