/* USAC MPEG Surround 212, parameter decoder (stage M1 + M2a of docs/USAC_MPS212_PLAN.md): Mps212Data(), EcData with its Huffman / PCM codes, mapping of the coded groups to the parameter bands, interpolation,
 * dequantisation, smoothing data.  Written for Sauti after studying decoder/ixheaacd_mps_parse.c and ixheaacd_mps_dec.c of libxaac (Apache 2.0); the data tables come from there (sauti_usac_mps_tables.c). */
#include "sauti_usac_mps.h"
#include "sauti_usac_mps_tables.h"
#include <math.h>
#include <string.h>

enum { CLD = 0, ICC = 1, IPD = 2 };
#define DIFF_FREQ 0
#define DIFF_TIME 1
#define MAXB SAUTI_USAC_MPS_BANDS

static const unsigned freq_res_bands[8] = { 0, 28, 20, 14, 10, 7, 5, 4 };
static const unsigned stride_table[4] = { 1, 2, 5, 28 };
static const uint16_t smooth_time_table[4] = { 64, 128, 256, 512 };

/* ---- Huffman node tables: node > 0 is the next node, node <= 0 the value -(node + 1) ---- */
static int huff_read(SautiUsacBits *b, const int16_t (*tab)[2], unsigned n, int *out) {
    unsigned node = 0, guard = 0;
    for (;;) {
        const int next = tab[node][sauti_usac_get(b, 1)];
        if (next <= 0) { *out = next; return 1; }
        if ((unsigned)next >= n || ++guard > n) return 0;
        node = (unsigned)next;
    }
}
#define HREAD(b, tab, out) huff_read((b), (tab), (unsigned)(sizeof(tab) / sizeof((tab)[0])), (out))

static int p0_read(SautiUsacBits *b, int type, int *v) {
    int node;
    switch (type) {
    case CLD: if (!HREAD(b, sauti_usac_mps_cld_p0, &node)) return 0; break;
    case ICC: if (!HREAD(b, sauti_usac_mps_icc_p0, &node)) return 0; break;
    default: if (!HREAD(b, sauti_usac_mps_ipd_hp0, &node)) return 0; break;
    }
    *v = -(node + 1);
    return 1;
}

static int huff_1d(SautiUsacBits *b, int type, int diff, int *out, unsigned n, int first) {
    unsigned i = 0;
    if (first) { if (!p0_read(b, type, &out[0])) return 0; i = 1; }
    for (; i < n; i++) {
        int node, od;
        switch (type) {
        case CLD: if (!HREAD(b, sauti_usac_mps_cld_1d[diff], &node)) return 0; break;
        case ICC: if (!HREAD(b, sauti_usac_mps_icc_1d[diff], &node)) return 0; break;
        default: if (!HREAD(b, sauti_usac_mps_ipd_1d[diff], &node)) return 0; break;
        }
        od = -(node + 1);
        if (type != IPD && od != 0 && sauti_usac_get(b, 1)) od = -od;
        out[i] = od;
    }
    return 1;
}

/* num_levels of a PCM codeword and the largest group of values that shares one codeword */
static unsigned pcm_group(unsigned levels) {
    switch (levels) {
    case 3: return 5; case 7: return 6; case 11: return 2; case 13: return 4; case 19: return 4; case 25: return 3; case 51: return 4;
    case 4: case 8: case 15: case 16: case 26: case 31: return 1;
    default: return 0;
    }
}
/* bits of a group of `len` values of `levels` levels: ceil(len * log2(levels)), exactly */
static unsigned pcm_bits(unsigned levels, unsigned len) {
    uint64_t pw = 1, target = 1; unsigned bits = 0, i;
    for (i = 0; i < len; i++) target *= levels;
    while (pw < target) { pw <<= 1; bits++; }
    return bits;
}
/* two interleaved outputs (out1 gets the even positions, out2 the odd) or one (out2 NULL) */
static int pcm_decode(SautiUsacBits *b, int *out1, int *out2, int offset, unsigned num_val, unsigned levels) {
    const unsigned group = pcm_group(levels);
    unsigned i, j;
    if (!group) return 0;
    for (i = 0; i < num_val; i += group) {
        const unsigned len = num_val - i < group ? num_val - i : group;
        uint32_t v = sauti_usac_get(b, pcm_bits(levels, len));
        for (j = 0; j < len; j++) {
            const unsigned idx = i + (len - j - 1u), next = v % levels;
            if (!out2) out1[idx] = (int)next - offset;
            else if (idx & 1u) out2[idx / 2u] = (int)next - offset;
            else out1[idx / 2u] = (int)next - offset;
            v = (v - next) / levels;
        }
    }
    return 1;
}

static void sym_restore(SautiUsacBits *b, int lav, int d[2], int ipd) {
    const int sum = d[0] + d[1], diff = d[0] - d[1];
    if (sum > lav) { d[0] = -sum + (2 * lav + 1); d[1] = -diff; } else { d[0] = sum; d[1] = diff; }
    if (!ipd && d[0] + d[1] != 0 && sauti_usac_get(b, 1)) { d[0] = -d[0]; d[1] = -d[1]; }
    if (d[0] - d[1] != 0 && sauti_usac_get(b, 1)) { const int t = d[0]; d[0] = d[1]; d[1] = t; }
}

/* the table of one lav of the 2-D codes: nodes, count */
static int table_2d(int type, int lav, int diff, int pair, const int16_t (**tab)[2], unsigned *n) {
#define T2D(NAME, LAV, ARR) case LAV: *tab = ARR[diff][pair]; *n = (unsigned)(sizeof(ARR[0][0]) / sizeof(ARR[0][0][0])); return 1;
    switch (type) {
    case CLD: switch (lav) { T2D(cld, 3, sauti_usac_mps_cld_2d_lav3) T2D(cld, 5, sauti_usac_mps_cld_2d_lav5) T2D(cld, 7, sauti_usac_mps_cld_2d_lav7) T2D(cld, 9, sauti_usac_mps_cld_2d_lav9) default: return 0; }
    case ICC: switch (lav) { T2D(icc, 1, sauti_usac_mps_icc_2d_lav1) T2D(icc, 3, sauti_usac_mps_icc_2d_lav3) T2D(icc, 5, sauti_usac_mps_icc_2d_lav5) T2D(icc, 7, sauti_usac_mps_icc_2d_lav7) default: return 0; }
    default: switch (lav) { T2D(ipd, 1, sauti_usac_mps_ipd_2d_lav1) T2D(ipd, 3, sauti_usac_mps_ipd_2d_lav3) T2D(ipd, 5, sauti_usac_mps_ipd_2d_lav5) T2D(ipd, 7, sauti_usac_mps_ipd_2d_lav7) default: return 0; }
    }
#undef T2D
}

/* num_val values as pairs (ch_fac 2: frequency pairs; 1: the two values of a time pair), first values (p0a, p0b) read in front when given.  pairs[i] for i = 0, ch_fac, 2 ch_fac ... */
static int huff_2d(SautiUsacBits *b, int type, int diff, int pair, int out[][2], unsigned num_val, unsigned ch_fac, int *p0a, int *p0b) {
    int node, lav, esc_idx[MAXB], esc0[MAXB], esc1[MAXB];
    unsigned i, nesc = 0;
    if (!HREAD(b, sauti_usac_mps_lav_idx, &node)) return 0;
    {
        const int data = -(node + 1);
        lav = type == CLD ? 2 * data + 3 : type == ICC ? 2 * data + 1 : 2 * (data == 0 ? 3 : data - 1) + 1;
    }
    if (p0a && !p0_read(b, type, p0a)) return 0;
    if (p0b && !p0_read(b, type, p0b)) return 0;
    for (i = 0; i < num_val; i += ch_fac) {
        const int16_t (*tab)[2]; unsigned n; int v, escape;
        if (!table_2d(type, lav, diff, pair, &tab, &n) || !huff_read(b, tab, n, &v)) return 0;
        escape = v == 0;
        if (escape) { out[i][0] = 0; out[i][1] = 1; }
        else { const int c = -(v + 1); out[i][0] = c >> 4; out[i][1] = c & 0xf; }
        if (escape) { if (nesc >= MAXB) return 0; esc_idx[nesc++] = (int)i; }
        else sym_restore(b, lav, out[i], type == IPD);
    }
    if (nesc) {
        unsigned k;
        if (!pcm_decode(b, esc0, esc1, 0, 2u * nesc, (unsigned)(2 * lav + 1))) return 0;
        for (k = 0; k < nesc; k++) { out[esc_idx[k]][0] = esc0[k] - lav; out[esc_idx[k]][1] = esc1[k] - lav; }
    }
    return 1;
}

/* one coded value or pair of sets (EcData pair): histories, differential decoding in time or frequency.  data_bands values per set are written to out1 (and out2 for a pair). */
static int ec_pair(SautiUsacBits *b, int type, int *history, unsigned data_bands, int pair_flag, int coarse, int diff_time_back, int *out1, int *out2) {
    int attach_lsb = 0, offset, levels, pcm_coding;
    int data1[MAXB], data2[MAXB], diff1[MAXB], diff2[MAXB];
    int diff_type[2] = { DIFF_FREQ, DIFF_FREQ };
    unsigned i;
    memset(data1, 0, sizeof data1); memset(data2, 0, sizeof data2); memset(diff1, 0, sizeof diff1); memset(diff2, 0, sizeof diff2);
    switch (type) {
    case CLD: levels = coarse ? 15 : 31; offset = coarse ? 7 : 15; break;
    case ICC: levels = coarse ? 4 : 8; offset = 0; break;
    default: levels = coarse ? 8 : 16; offset = 0; attach_lsb = !coarse; break;
    }
    pcm_coding = (int)sauti_usac_get(b, 1);
    if (data_bands == 0 || data_bands > MAXB) return 0;
    if (pcm_coding) {
        if (pair_flag) { if (!pcm_decode(b, data1, data2, offset, 2u * data_bands, (unsigned)levels)) return 0; }
        else if (!pcm_decode(b, data1, NULL, offset, data_bands, (unsigned)levels)) return 0;
    } else {
        int scheme_2d, freq_pair = 1, direction = 0, mixed;
        if (pair_flag || diff_time_back) diff_type[0] = (int)sauti_usac_get(b, 1);
        if (pair_flag && (diff_type[0] == DIFF_FREQ || diff_time_back)) diff_type[1] = (int)sauti_usac_get(b, 1);
        scheme_2d = (int)sauti_usac_get(b, 1);                                                      /* cdg_scheme: 0 = Huffman 1-D, 1 = 2-D */
        if (scheme_2d && pair_flag) freq_pair = sauti_usac_get(b, 1) == 0u;                           /* 0 = frequency pair, 1 = time pair */
        if (!scheme_2d) {
            if (!huff_1d(b, type, diff_type[0], diff1, data_bands, diff_type[0] == DIFF_FREQ)) return 0;
            if (pair_flag && !huff_1d(b, type, diff_type[1], diff2, data_bands, diff_type[1] == DIFF_FREQ)) return 0;
        } else if (freq_pair) {
            for (i = 0; i < (pair_flag ? 2u : 1u); i++) {
                int *dv = i ? diff2 : diff1, pairs[MAXB][2];
                unsigned num = data_bands, off = 0, rest;
                int *p0 = NULL;
                if (diff_type[i] == DIFF_FREQ) { p0 = &dv[0]; num--; off = 1; }
                rest = num % 2u; num -= rest;
                if (!huff_2d(b, type, diff_type[i], 0, pairs, num, 2, p0, NULL)) return 0;
                {   unsigned k; for (k = 0; k + 1 < num; k += 2) { dv[off + k] = pairs[k][0]; dv[off + k + 1] = pairs[k][1]; } }
                if (rest && !huff_1d(b, type, diff_type[i], dv + off + num, 1, 0)) return 0;
            }
        } else {                                                                                    /* time pair */
            int pairs[MAXB][2], *p0a = NULL, *p0b = NULL, df;
            unsigned num = data_bands, off = 0, k;
            if (diff_type[0] == DIFF_FREQ || diff_type[1] == DIFF_FREQ) { p0a = &diff1[0]; p0b = &diff2[0]; off = 1; num--; }
            df = (diff_type[0] == DIFF_TIME || diff_type[1] == DIFF_TIME) ? DIFF_TIME : DIFF_FREQ;
            if (!huff_2d(b, type, df, 1, pairs, num, 1, p0a, p0b)) return 0;
            for (k = 0; k < num; k++) { diff1[off + k] = pairs[k][0]; diff2[off + k] = pairs[k][1]; }
        }
        if (diff_type[0] == DIFF_TIME || diff_type[1] == DIFF_TIME) {
            if (pair_flag) {
                if (diff_type[0] == DIFF_TIME && !diff_time_back) direction = 1;                      /* FORWARDS */
                else if (diff_type[1] == DIFF_TIME) direction = 0;
                else direction = (int)sauti_usac_get(b, 1);
            }
        }
        mixed = diff_type[0] != diff_type[1] && scheme_2d && pair_flag && !freq_pair;
        if (!direction) {
            if (diff_type[0] == DIFF_FREQ) { data1[0] = diff1[0]; for (i = 1; i < data_bands; i++) data1[i] = data1[i - 1] + diff1[i]; }
            else {
                int msb[MAXB];
                for (i = 0; i < data_bands; i++) { msb[i] = history[i] + offset; if (attach_lsb) msb[i] >>= 1; }
                for (i = 0; i < data_bands; i++) data1[i] = (mixed && i == 0) ? diff1[0] : msb[i] + diff1[i];
            }
            if (pair_flag) {
                if (diff_type[1] == DIFF_FREQ) { data2[0] = diff2[0]; for (i = 1; i < data_bands; i++) data2[i] = data2[i - 1] + diff2[i]; }
                else for (i = 0; i < data_bands; i++) data2[i] = (mixed && i == 0) ? diff2[0] : data1[i] + diff2[i];
            }
        } else {
            data2[0] = diff2[0]; for (i = 1; i < data_bands; i++) data2[i] = data2[i - 1] + diff2[i];
            if (diff_type[0] == DIFF_FREQ) { data1[0] = diff1[0]; for (i = 1; i < data_bands; i++) data1[i] = data1[i - 1] + diff1[i]; }
            else for (i = 0; i < data_bands; i++) data1[i] = (mixed && i == 0) ? diff1[0] : data2[i] - diff1[i];
        }
        for (i = 0; i < data_bands; i++) {                                                          /* attach the least significant bit (fine IPD), remove the offset */
            const int lsb1 = attach_lsb ? (int)sauti_usac_get(b, 1) : 0;
            data1[i] = (attach_lsb ? (data1[i] * 2 + lsb1) : data1[i]) - offset;
        }
        if (pair_flag) for (i = 0; i < data_bands; i++) {
            const int lsb2 = attach_lsb ? (int)sauti_usac_get(b, 1) : 0;
            data2[i] = (attach_lsb ? (data2[i] * 2 + lsb2) : data2[i]) - offset;
        }
    }
    memcpy(out1, data1, sizeof(int) * data_bands);
    if (pair_flag) memcpy(out2, data2, sizeof(int) * data_bands);
    return 1;
}

static void coarse2fine(int *data, int type, unsigned n) {
    unsigned i;
    for (i = 0; i < n; i++) data[i] *= 2;
    if (type == CLD) for (i = 0; i < n; i++) { if (data[i] == -14) data[i] = -15; else if (data[i] == 14) data[i] = 15; }
}
static void fine2coarse(int *data, unsigned n) { unsigned i; for (i = 0; i < n; i++) data[i] /= 2; }

/* strides of the coded groups: the parse uses this map ... */
static unsigned stride_map(unsigned stride_idx, unsigned stop, unsigned *strides) {
    const unsigned fac = stride_table[stride_idx];
    const unsigned bands = (stop - 1u) / fac + 1u;
    unsigned pb, off = 0, i;
    strides[0] = 0;
    for (pb = 1; pb <= bands; pb++) strides[pb] = strides[pb - 1] + fac;
    while (strides[bands] > stop) {
        if (off < bands) off++; else off = 1;
        for (i = off; i <= bands; i++) strides[i]--;
    }
    return bands;
}
/* ... and the mapping to bands uses this one (the two differ for 28 bands with stride 2 / 5) */
static unsigned create_mapping(unsigned *map, unsigned stop, unsigned fac) {
    const unsigned out_bands = (stop - 1u) / fac + 1u;
    int vdk[MAXB + 1], diff, incr, k;
    unsigned i;
    diff = (int)stop - (int)(out_bands * fac);
    for (i = 0; i < out_bands; i++) vdk[i] = (int)fac;
    if (diff > 0) { incr = -1; k = (int)out_bands - 1; } else { incr = 1; k = 0; }
    while (diff != 0) {
        vdk[k] -= incr; k += incr; diff += incr;
        if (k >= (int)out_bands) { if (diff > 0) k = (int)out_bands - 1; else if (diff < 0) k = 0; }
        if (k < 0) k = 0;
    }
    map[0] = 0;
    for (i = 0; i < out_bands; i++) map[i + 1] = map[i] + (unsigned)vdk[i];
    return out_bands;
}

typedef struct {
    unsigned mode[SAUTI_USAC_MPS_SETS];
    unsigned coarse[SAUTI_USAC_MPS_SETS], stride[SAUTI_USAC_MPS_SETS];
    int cmp[SAUTI_USAC_MPS_SETS][MAXB];                  /* the coded (grouped) indices of the sets that carry data */
} EcSets;

static int ec_data(SautiUsacBits *b, int type, unsigned num_sets, unsigned band_stop, int indep, int *last, unsigned *coarse_prev, EcSets *d) {
    unsigned i, set_index = 0, pair = 0, old_coarse = *coarse_prev;
    for (i = 0; i < num_sets; i++) d->mode[i] = sauti_usac_get(b, 2);
    for (i = 0; i < num_sets; i++) {
        if (d->mode[i] == 0) { unsigned pb; for (pb = 0; pb < band_stop; pb++) last[pb] = 0; old_coarse = 0; }
        if (d->mode[i] == 3) {
            if (pair) pair = 0;
            else {
                unsigned strides[MAXB + 1], bands, pb, j;
                pair = sauti_usac_get(b, 1);
                d->coarse[set_index] = sauti_usac_get(b, 1);
                d->stride[set_index] = sauti_usac_get(b, 2);
                if (d->coarse[set_index] != old_coarse) { if (old_coarse) coarse2fine(last, type, band_stop); else fine2coarse(last, band_stop); }
                bands = stride_map(d->stride[set_index], band_stop, strides);
                for (pb = 0; pb < bands; pb++) last[pb] = last[strides[pb]];
                if (set_index + pair >= SAUTI_USAC_MPS_SETS) return 0;
                if (!ec_pair(b, type, last, bands, (int)pair, (int)d->coarse[set_index], !(indep && i == 0) || set_index > 0, d->cmp[set_index], d->cmp[set_index + pair])) return 0;
                for (pb = 0; pb < bands; pb++)
                    for (j = strides[pb]; j < strides[pb + 1]; j++) last[j] = type == IPD ? (d->coarse[set_index] ? d->cmp[set_index + pair][pb] & 7 : d->cmp[set_index + pair][pb] & 15) : d->cmp[set_index + pair][pb];
                old_coarse = d->coarse[set_index];
                if (pair) { d->coarse[set_index + 1] = d->coarse[set_index]; d->stride[set_index + 1] = d->stride[set_index]; }
                set_index += pair + 1u;
            }
        }
    }
    return 1;
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* coded groups -> bands, hold / interpolate, coarse -> fine, clamp, dequantise */
static int map_data(const EcSets *d, int type, unsigned num_sets, const unsigned *slots, unsigned band_stop, int *idx_prev, unsigned *coarse_prev, int out_idx[][MAXB], float out[][MAXB]) {
    unsigned interp[SAUTI_USAC_MPS_SETS + 1], coarse_flag[SAUTI_USAC_MPS_SETS], i, band, set_index = 0;
    memset(interp, 0, sizeof interp); memset(coarse_flag, 0, sizeof coarse_flag);
    for (i = 0; i < num_sets; i++) {
        if (d->mode[i] == 0) {
            coarse_flag[i] = 0;
            for (band = 0; band < band_stop; band++) out_idx[i][band] = idx_prev[band] = 0;
            *coarse_prev = 0;
        }
        if (d->mode[i] == 1 || d->mode[i] == 2) {
            for (band = 0; band < band_stop; band++) out_idx[i][band] = idx_prev[band];
            coarse_flag[i] = *coarse_prev;
            interp[i] = d->mode[i] == 2;
        }
        if (d->mode[i] == 3) {
            const unsigned fac = stride_table[d->stride[set_index]];
            unsigned map[MAXB + 1], bands = create_mapping(map, band_stop, fac), g, j;
            for (g = 0; g < bands; g++) for (j = map[g]; j < map[g + 1]; j++) out_idx[i][j] = d->cmp[set_index][g];
            for (band = 0; band < band_stop; band++) idx_prev[band] = out_idx[i][band];
            *coarse_prev = d->coarse[set_index];
            coarse_flag[i] = d->coarse[set_index];
            set_index++;
        }
    }
    for (i = 0; i < num_sets; i++) if (coarse_flag[i]) coarse2fine(out_idx[i], type, band_stop);
    {
        int i1 = -1;
        for (i = 0; i < num_sets; i++) {
            unsigned i2 = i;
            int x1, xi, x2;
            if (!interp[i]) i1 = (int)i;
            while (interp[i2]) i2++;
            if (i1 < 0) { i1 = 0; x1 = 0; } else x1 = (int)slots[i1];
            xi = (int)slots[i];
            if (interp[i]) {
                if (i2 >= num_sets) return 0;                                                       /* nothing to interpolate towards */
                x2 = (int)slots[i2];
                for (band = 0; band < band_stop; band++) {
                    int y1 = out_idx[i1][band], y2 = out_idx[i2][band], yi = 0;
                    if (type == IPD) {
                        if (y2 - y1 > 8) y1 += 16;
                        if (y1 - y2 > 8) y2 += 16;
                        if (x2 != x1) yi = (y1 + (xi - x1) * (y2 - y1) / (x2 - x1)) % 16;
                    } else if (x2 != x1) yi = y1 + (xi - x1) * (y2 - y1) / (x2 - x1);
                    out_idx[i][band] = yi;
                }
            }
        }
    }
    for (i = 0; i < num_sets; i++)
        for (band = 0; band < band_stop; band++) {
            if (type == CLD) { out_idx[i][band] = clampi(out_idx[i][band], -15, 15); out[i][band] = sauti_usac_mps_cld_dequant[out_idx[i][band] + 15]; }
            else if (type == ICC) { out_idx[i][band] = clampi(out_idx[i][band], 0, 7); out[i][band] = sauti_usac_mps_icc_dequant[out_idx[i][band]]; }
            else out[i][band] = sauti_usac_mps_ipd_dequant[out_idx[i][band] & 15];
        }
    return 1;
}

int sauti_usac_mps212_parse(const SautiUsacMpsConfig *e, unsigned time_slots, SautiUsacBits *b, int indep, SautiUsacMpsState *st, SautiUsacMpsFrame *f) {
    SautiUsacMpsState ns = *st;
    SautiUsacMpsFrame nf_local, *nf = &nf_local;
    EcSets cld, icc, ipd;
    unsigned i, sets, bands, num_bands_ipd, ext;
    if (!e || !b || !st || !f || (time_slots != 32u && time_slots != 64u) || e->freq_res == 0u || e->freq_res > 7u) return 0;
    bands = freq_res_bands[e->freq_res];
    num_bands_ipd = e->bands_phase;
    memset(nf, 0, sizeof *nf);
    memset(&cld, 0, sizeof cld); memset(&icc, 0, sizeof icc); memset(&ipd, 0, sizeof ipd);
    memcpy(ipd.coarse, ns.ipd_coarse_hist, sizeof ipd.coarse);
    nf->param_bands = bands;
    if (e->high_rate) {
        const unsigned framing = sauti_usac_get(b, 1);
        sets = sauti_usac_get(b, 3) + 1u;
        if (framing) {
            int prev = -1;
            for (i = 0; i < sets; i++) {
                nf->param_slot[i] = sauti_usac_get(b, time_slots == 32u ? 5u : 6u);
                if (prev >= (int)nf->param_slot[i] || nf->param_slot[i] >= time_slots) return 0;
                prev = (int)nf->param_slot[i];
            }
        } else for (i = 0; i < sets; i++) nf->param_slot[i] = (time_slots * (i + 1u) + sets - 1u) / sets - 1u;
    } else {
        sets = 1;
        nf->param_slot[0] = time_slots - 1u;
    }
    nf->independency = indep ? 1u : sauti_usac_get(b, 1);
    if (!ec_data(b, CLD, sets, bands, (int)nf->independency, ns.cmp_cld_prev, &ns.quant_coarse_prev[0], &cld)) return 0;
    if (!ec_data(b, ICC, sets, bands, (int)nf->independency, ns.cmp_icc_prev, &ns.quant_coarse_prev[1], &icc)) return 0;
    if (e->phase_coding) {
        nf->phase_mode = sauti_usac_get(b, 1);
        if (nf->phase_mode) {
            nf->opd_smoothing = sauti_usac_get(b, 1);
            if (num_bands_ipd > MAXB || !ec_data(b, IPD, sets, num_bands_ipd, (int)nf->independency, ns.cmp_ipd_prev, &ns.quant_coarse_prev[2], &ipd)) return 0;
        } else { memset(ns.cmp_ipd_prev, 0, sizeof ns.cmp_ipd_prev); memset(ns.ipd_pre, 0, sizeof ns.ipd_pre); }
    }
    if (e->high_rate) {
        unsigned ps;
        for (ps = 0; ps < sets; ps++) {
            const unsigned mode = sauti_usac_get(b, 2);
            unsigned time_idx = 0, pb;
            uint8_t sd[MAXB];
            memset(sd, 0, sizeof sd);
            if (mode >= 2u) time_idx = sauti_usac_get(b, 2);
            if (mode == 3u) {
                const unsigned stride = sauti_usac_get(b, 2), fac = stride_table[stride], groups = (bands - 1u) / fac + 1u;
                unsigned map[MAXB + 1], g, j;
                uint8_t gd[MAXB];
                for (g = 0; g < groups; g++) gd[g] = (uint8_t)sauti_usac_get(b, 1);
                create_mapping(map, bands, fac);
                for (g = 0; g < groups; g++) for (j = map[g]; j < map[g + 1]; j++) sd[j] = gd[g];
            }
            switch (mode) {
            case 0: nf->smooth_time[ps] = 256; break;
            case 1:
                nf->smooth_time[ps] = ps ? nf->smooth_time[ps - 1u] : ns.smooth_time_prev;
                for (pb = 0; pb < bands; pb++) nf->smooth_data[ps][pb] = ps ? nf->smooth_data[ps - 1u][pb] : ns.smooth_data_prev[pb];
                break;
            case 2:
                nf->smooth_time[ps] = smooth_time_table[time_idx];
                for (pb = 0; pb < bands; pb++) nf->smooth_data[ps][pb] = 1;
                break;
            default:
                nf->smooth_time[ps] = smooth_time_table[time_idx];
                for (pb = 0; pb < bands; pb++) nf->smooth_data[ps][pb] = sd[pb];
                break;
            }
        }
    } else nf->smooth_time[0] = 256;
    switch (e->temp_shape) {                           /* temporal shaping: only frames that do not use it are handled */
    case 0: break;
    case 3:                                                                    /* TSD: the time slots of the transients (a combinatorial codeword) and a 3 bit phase for each */
        nf->tsd_enable = sauti_usac_get(b, 1);
        if (nf->tsd_enable) {
            static uint64_t binom[65][33]; static int ready;
            unsigned ntr, len, k, p, bit;
            uint64_t sw = 0;
            if (!ready) { unsigned n, r; for (n = 0; n <= 64u; n++) for (r = 0; r <= 32u; r++) binom[n][r] = r == 0u ? 1u : (n == 0u ? 0u : binom[n - 1u][r - 1u] + (r <= n - 1u ? binom[n - 1u][r] : 0u)); ready = 1; }
            ntr = sauti_usac_get(b, time_slots == 32u ? 4u : 5u) + 1u;
            { const uint64_t c = binom[time_slots][ntr]; len = 0; while (len < 64u && (((c - 1u) >> len) != 0u)) len++; }   /* bits of the codeword: those of the number of ways to place ntr transients */
            for (bit = 0; bit < len; bit++) sw = (sw << 1) | sauti_usac_get(b, 1);
            memset(nf->tsd_sep, 0, sizeof nf->tsd_sep);
            p = ntr;
            for (k = time_slots; k-- > 0u;) {                                   /* combinatorial number system, positions from the last slot down */
                if (p > k) { for (;; k--) { nf->tsd_sep[k] = 1; if (k == 0u) break; } break; }
                if (sw >= binom[k][p]) { sw -= binom[k][p]; nf->tsd_sep[k] = 1; if (--p == 0u) break; }
            }
            for (k = 0; k < time_slots; k++) if (nf->tsd_sep[k]) nf->tsd_phase[k] = sauti_usac_get(b, 3);
        }
        break;
    case 1: nf->stp_enable = sauti_usac_get(b, 1); nf->stp_ch[0] = nf->stp_ch[1] = 0u;                    /* STP: bsTempShapeEnable, then one flag per output channel */
            if (nf->stp_enable) { nf->stp_ch[0] = sauti_usac_get(b, 1); nf->stp_ch[1] = sauti_usac_get(b, 1); }
            break;
    case 2:                                                                    /* GES: per channel a flag, then the envelope as (value, run length) pairs coded with the reshape Huffman table */
        if (sauti_usac_get(b, 1)) {
            unsigned ch, ts;
            nf->ges_ch[0] = sauti_usac_get(b, 1); nf->ges_ch[1] = sauti_usac_get(b, 1);
            for (ch = 0; ch < 2u; ch++) {
                unsigned got = 0;
                if (!nf->ges_ch[ch]) continue;
                while (got < time_slots) {
                    int node, val, len;
                    if (!HREAD(b, sauti_usac_mps_reshape, &node)) return 0;
                    if (node == 0) { val = 0; len = 2; }                                  /* the escape code: value 0, two slots */
                    else { const int v8 = -(node + 1); val = v8 >> 4; len = (v8 & 15) + 1; }
                    for (ts = 0; ts < (unsigned)len && got < time_slots; ts++, got++) nf->ges_env[ch][got] = (float)pow(2.0, (double)(float)val / (double)(e->env_quant + 2u) - 1.0);
                }
            }
        }
        break;
    default: return 0;
    }
    if (b->pos > b->nbits) return 0;
    /* parameters: mapping, interpolation, dequantisation */
    ext = nf->param_slot[sets - 1u] != time_slots - 1u;
    nf->ext_frame = ext;
    nf->num_sets = sets;
    if (!map_data(&cld, CLD, sets, nf->param_slot, bands, ns.cld_pre, &ns.quant_coarse_prev[0], nf->cld_idx, nf->cld)) return 0;
    if (!map_data(&icc, ICC, sets, nf->param_slot, bands, ns.icc_pre, &ns.quant_coarse_prev[1], nf->icc_idx, nf->icc)) return 0;
    if (e->phase_coding && !map_data(&ipd, IPD, sets, nf->param_slot, bands, ns.ipd_pre, &ns.quant_coarse_prev[2], nf->ipd_idx, nf->ipd)) return 0;
    if (e->phase_coding) { unsigned i; for (i = 0; i < SAUTI_USAC_MPS_SETS; i++) nf->ipd_coarse[i] = ipd.coarse[i]; memcpy(ns.ipd_coarse_hist, ipd.coarse, sizeof ns.ipd_coarse_hist); }                  /* the reference indexes the flag by the coded set (not by the parameter set) when it smooths the phases */
    for (i = 0; i < sets; i++) {                                                                  /* the same checks as the reference makes on the distances of the sets */
        const unsigned diff = i ? nf->param_slot[i] - nf->param_slot[i - 1u] : nf->param_slot[0] + 1u;
        if (diff == 0 || diff > 72u) return 0;
    }
    ns.smooth_time_prev = nf->smooth_time[sets - 1u];
    memcpy(ns.smooth_data_prev, nf->smooth_data[sets - 1u], sizeof ns.smooth_data_prev);
    if (ext) {                                                                                      /* a set is appended in the last slot: a copy of the last one */
        memcpy(nf->cld_idx[sets], nf->cld_idx[sets - 1u], sizeof nf->cld_idx[0]); memcpy(nf->cld[sets], nf->cld[sets - 1u], sizeof nf->cld[0]);
        memcpy(nf->icc_idx[sets], nf->icc_idx[sets - 1u], sizeof nf->icc_idx[0]); memcpy(nf->icc[sets], nf->icc[sets - 1u], sizeof nf->icc[0]);
        memcpy(nf->ipd_idx[sets], nf->ipd_idx[sets - 1u], sizeof nf->ipd_idx[0]); memcpy(nf->ipd[sets], nf->ipd[sets - 1u], sizeof nf->ipd[0]);
        nf->smooth_time[sets] = nf->smooth_time[sets - 1u]; memcpy(nf->smooth_data[sets], nf->smooth_data[sets - 1u], sizeof nf->smooth_data[0]);
        nf->param_slot[sets] = time_slots - 1u;
        nf->num_sets = sets + 1u;
    }
    *st = ns; *f = *nf;
    return 1;
}
