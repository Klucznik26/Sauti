/* USAC LPD, stage L1: the algebraic vector quantiser (RE8 lattice, multi-rate Gosset codes) and the line spectral frequency (LSF) chain: dequantisation of the LSF sets, LSF <-> LSP, LSP -> prediction filter,
 * interpolation.  Float arithmetic; the lattice and codebook data are in sauti_usac_lpd_tables.c. */
#include "sauti_usac_lpd_dec.h"
#include "sauti_usac_lpd_tables.h"
#include <math.h>
#include <string.h>

/* ---- AVQ: decoding of a base codebook index (Q2 / Q3 / Q4) ---- */
static unsigned leader_interval(const uint32_t *table, uint32_t index, unsigned size) {
    unsigned i;
    for (i = 4; i < size; i += 4) if (index < table[i]) break;
    if (i > size) i = size;
    if (index < table[i - 2]) i -= 2;
    if (index < table[i - 1]) i--;
    return i - 1u;
}
/* the permutation of a leader with the given rank */
static int permutation_of_rank(int32_t rank, int32_t xs[8]) {
    int32_t a[8], w[8], base = 1;
    unsigned i, j = 0;
    w[0] = 1; a[0] = xs[0];
    for (i = 1; i < 8u; i++) {
        if (xs[i] != xs[i - 1]) { j++; w[j] = 1; a[j] = xs[i]; }
        else { w[j]++; base *= w[j]; }
    }
    if (w[0] == 8) { for (i = 0; i < 8u; i++) xs[i] = a[0]; return 1; }
    {
        int32_t target = rank * base, fac_b = 1;
        for (i = 0; i < 8u; i++) {
            const int32_t fac = fac_b * sauti_usac_lpd_factorial_7[i];
            int jj = -1;
            do { if (++jj > (int)j) return 0; target -= w[jj] * fac; } while (target >= 0);      /* a rank beyond the number of permutations: an index outside the codebook */
            xs[i] = a[jj];
            target += w[jj] * fac;
            fac_b *= w[jj];
            w[jj]--;
        }
    }
    return 1;
}
static int base_index_decode(unsigned n, uint32_t index, int32_t ya[8]) {
    unsigned i, idx = 0, t, im, ks, sign_code;
    if (n < 2u) { for (i = 0; i < 8u; i++) ya[i] = 0; return 1; }
    if (n == 4u) idx = (unsigned)sauti_usac_lpd_pos_abs_leaders_a4[leader_interval(sauti_usac_lpd_card_offset_i4, index, 28u)];
    else idx = (unsigned)sauti_usac_lpd_pos_abs_leaders_a3[leader_interval(sauti_usac_lpd_card_offset_i3, index, 9u)];
    if (idx >= 37u) return 0;
    for (i = 0; i < 8u; i++) ya[i] = sauti_usac_lpd_abs_leaders[idx][i];
    t = (unsigned)sauti_usac_lpd_iso_code_index[idx];
    im = (unsigned)sauti_usac_lpd_iso_code_num[idx];
    if (t >= 226u || im == 0u || t + im > 226u) return 0;
    ks = leader_interval(sauti_usac_lpd_signed_leader_is + t, index, im);
    sign_code = 2u * sauti_usac_lpd_iso_code_data[t + ks];
    for (i = 8; i-- > 0;) { ya[i] *= 1 - (int)(sign_code & 2u); sign_code >>= 1; }
    return permutation_of_rank((int32_t)(index - sauti_usac_lpd_signed_leader_is[t + ks]), ya);
}

/* ---- AVQ: Voronoi extension ---- */
static void nearest_neighbour(const int32_t x[8], int32_t y[8], unsigned count, int32_t rem[8]) {
    int32_t r[8], sum = 0, e[8];
    unsigned i, j = 0;
    memcpy(r, rem, sizeof r);
    for (i = 0; i < 8u; i++) {
        if (x[i] < 0) y[i] = -(((1 - x[i]) >> 1) << 1); else y[i] = ((1 + x[i]) >> 1) << 1;
        sum += y[i];
        if (x[i] % 2 != 0) r[i] = x[i] < 0 ? -(r[i] - (1 << count)) : r[i] - (1 << count);
    }
    if (sum % 4) {
        int32_t em = 0;
        for (i = 0; i < 8u; i++) e[i] = r[i];
        for (i = 0; i < 8u; i++) { const int32_t s = e[i] < 0 ? -e[i] : e[i]; if (em < s) { em = s; j = i; } }
        if (e[j] < 0) { y[j] -= 2; r[j] += 2 << count; } else { y[j] += 2; r[j] -= 2 << count; }
    }
    memcpy(rem, r, sizeof r);
}
static void voronoi_search(const int32_t x[8], int32_t y[8], unsigned count, int32_t rem1[8], int32_t rem2[8]) {
    int32_t y0[8], y1[8], x1[8];
    int64_t e0 = 0, e1 = 0;
    unsigned i;
    nearest_neighbour(x, y0, count, rem1);
    for (i = 0; i < 8u; i++) {
        if (x[i] == 0) {
            if (rem2[i] == 0) x1[i] = x[i] - 1;
            else { x1[i] = 0; rem2[i] -= 1 << count; }
        } else x1[i] = x[i] - 1;
    }
    nearest_neighbour(x1, y1, count, rem2);
    for (i = 0; i < 8u; i++) y1[i] += 1;
    for (i = 0; i < 8u; i++) { e0 += (int64_t)rem1[i] * rem1[i]; e1 += (int64_t)rem2[i] * rem2[i]; }
    memcpy(y, e0 < e1 ? y0 : y1, sizeof y0);
}
static void voronoi_decode(const uint16_t kv[8], int32_t m, int32_t y[8], unsigned count) {
    int32_t v[8], z[8], rem1[8], rem2[8], sum = 0, tmp;
    unsigned i;
    for (i = 0; i < 8u; i++) y[i] = kv[7];
    z[7] = y[7] >> count;
    rem1[7] = y[7] & (m - 1);
    for (i = 6; i >= 1u; i--) {
        tmp = (int32_t)kv[i] << 1;
        sum += tmp;
        y[i] += tmp;
        z[i] = y[i] >> count;
        rem1[i] = y[i] & (m - 1);
    }
    y[0] += 4 * (int32_t)kv[0] + sum;
    z[0] = (y[0] - 2) >> count;
    rem1[0] = (y[0] - 2) % m;
    memcpy(rem2, rem1, sizeof rem2);
    voronoi_search(z, v, count, rem1, rem2);
    for (i = 0; i < 8u; i++) y[i] -= m * v[i];
}

int sauti_usac_lpd_avq_decode(unsigned qn, uint32_t index, const uint16_t kv[8], int32_t out[8]) {
    unsigned i;
    if (qn > 35u) return 0;
    if (qn <= 4u) return base_index_decode(qn, index, out);
    {
        int32_t c[8], m;
        unsigned count = 0, q = qn;
        while (q > 4u) { count++; q -= 2u; }
        m = (int32_t)(1u << count);
        if (!base_index_decode(q, index, out)) return 0;
        voronoi_decode(kv, m, c, count);
        for (i = 0; i < 8u; i++) out[i] = m * out[i] + c[i];
    }
    return 1;
}

/* ---- LSF sets ---- */
#define LSF_GAP 50.0f
#define FREQ_MAX 6400.0f
static const float weight_factor[4] = { 60.0f, 65.0f, 64.0f, 63.0f };

static int avq_vector(const SautiUsacLpdLsf *s, int32_t v[16]) {
    unsigned k;
    for (k = 0; k < 2u; k++) {
        static const uint16_t zero[8] = { 0 };
        if (!sauti_usac_lpd_avq_decode(s->qn[k], s->index[k], s->qn[k] ? s->kv[k] : zero, v + 8u * k)) return 0;
    }
    return 1;
}
/* absolutely coded set: first-stage codebook vector plus the weighted AVQ residual, ordered and kept 50 Hz apart */
static int lsf_absolute(const SautiUsacLpdLsf *s, float *lsf) {
    int32_t v[16];
    float lo = LSF_GAP;
    unsigned i;
    if (!s->has_first_stage || s->first_stage > 255u || !avq_vector(s, v)) return 0;
    for (i = 0; i < 16u; i++) {
        lsf[i] = sauti_usac_lpd_lsf_codebook[s->first_stage][i] + sauti_usac_lpd_lsf_weight[s->first_stage][i] * (float)v[i];
        if (lsf[i] < lo) lsf[i] = lo;
        lo = lsf[i] + LSF_GAP;
    }
    lo = FREQ_MAX - LSF_GAP;
    for (i = 16; i-- > 0;) { if (lsf[i] > lo) lsf[i] = lo; lo = lsf[i] - LSF_GAP; }
    return 1;
}
/* relatively coded set: `lsf` holds the reference set on entry */
static int lsf_relative(const SautiUsacLpdLsf *s, float *lsf, unsigned mode) {
    int32_t v[16];
    float d[17], lo = LSF_GAP;
    unsigned i;
    if (!avq_vector(s, v)) return 0;
    d[0] = lsf[0]; d[16] = FREQ_MAX - lsf[15];
    for (i = 1; i < 16u; i++) d[i] = lsf[i] - lsf[i - 1];
    for (i = 0; i < 16u; i++) {
        const float w = (float)((weight_factor[mode] * sqrt(d[i] * d[i + 1])) / 400.0f);
        lsf[i] += w * (float)v[i];
        if (lsf[i] < lo) lsf[i] = lo;
        lo = lsf[i] + LSF_GAP;
    }
    return 1;
}

int sauti_usac_lpd_lsf_dequant(const SautiUsacLpdFrame *f, float lsf[5][SAUTI_LPD_ORDER]) {
    unsigned i;
    if (!lsf_absolute(&f->lsf[4], lsf[4])) return 0;
    if (f->first_lpd_flag) {
        const SautiUsacLpdLsf *s = &f->lsf[0];
        if (s->mode_lpc == 0u) { if (!lsf_absolute(s, lsf[0])) return 0; }
        else { memcpy(lsf[0], lsf[4], sizeof lsf[0]); if (!lsf_relative(s, lsf[0], 3)) return 0; }
    }
    if (f->mod[0] < 3u) {
        const SautiUsacLpdLsf *s = &f->lsf[2];
        if (s->mode_lpc == 0u) { if (!lsf_absolute(s, lsf[2])) return 0; }
        else { memcpy(lsf[2], lsf[4], sizeof lsf[2]); if (!lsf_relative(s, lsf[2], 3)) return 0; }
    }
    if (f->mod[0] < 2u) {
        const SautiUsacLpdLsf *s = &f->lsf[1];
        if (s->mode_lpc == 1u) for (i = 0; i < 16u; i++) lsf[1][i] = 0.5f * (lsf[0][i] + lsf[2][i]);
        else if (s->mode_lpc == 0u) { if (!lsf_absolute(s, lsf[1])) return 0; }
        else { memcpy(lsf[1], lsf[2], sizeof lsf[1]); if (!lsf_relative(s, lsf[1], 2)) return 0; }
    }
    if (f->mod[2] < 2u) {
        const SautiUsacLpdLsf *s = &f->lsf[3];
        switch (s->mode_lpc) {
        case 0: if (!lsf_absolute(s, lsf[3])) return 0; break;
        case 1: for (i = 0; i < 16u; i++) lsf[3][i] = 0.5f * (lsf[2][i] + lsf[4][i]); if (!lsf_relative(s, lsf[3], 1)) return 0; break;
        case 2: memcpy(lsf[3], lsf[2], sizeof lsf[3]); if (!lsf_relative(s, lsf[3], 2)) return 0; break;
        default: memcpy(lsf[3], lsf[4], sizeof lsf[3]); if (!lsf_relative(s, lsf[3], 2)) return 0; break;
        }
    }
    return 1;
}

/* ---- LSF / LSP / LPC ---- */
void sauti_usac_lpd_lsf_to_lsp(const float *lsf, float *lsp) {
    unsigned i;
    for (i = 0; i < 16u; i++) lsp[i] = (float)cos((double)lsf[i] * (3.14159265358979323846 / 6400.0));
}
void sauti_usac_lpd_lsp_to_lsf(const float *lsp, float *lsf) {
    unsigned i;
    for (i = 0; i < 16u; i++) lsf[i] = (float)(acos(lsp[i]) * (6400.0 / 3.14159265358979323846));
}
static void lsp_polynomials(const float *lsp, float *f1, float *f2) {
    float *p1 = f1, *p2 = f2;
    int i, j;
    const float *q = lsp;
    p1[0] = p2[0] = 1.0f;
    for (i = 1; i <= 8; i++) {
        const float b1 = -2.0f * (*q++), b2 = -2.0f * (*q++);
        p1[i] = b1 * p1[i - 1] + 2.0f * p1[i - 2];
        p2[i] = b2 * p2[i - 1] + 2.0f * p2[i - 2];
        for (j = i - 1; j > 0; j--) { p1[j] += b1 * p1[j - 1] + p1[j - 2]; p2[j] += b2 * p2[j - 1] + p2[j - 2]; }
    }
}
void sauti_usac_lpd_lsp_to_lpc(const float *lsp, float *a) {
    float poly1[10], poly2[10], *p1, *p2;
    unsigned i;
    poly1[0] = poly2[0] = 0.0f;
    /* f1[-1] and f2[-1] of the original recursion are 0: p[i - 2] for i = 1 is the (zero) element in front of the polynomial */
    {
        float t1[11], t2[11];
        t1[0] = t2[0] = 0.0f;
        lsp_polynomials(lsp, t1 + 1, t2 + 1);
        memcpy(poly1 + 1, t1 + 1, 9 * sizeof(float));
        memcpy(poly2 + 1, t2 + 1, 9 * sizeof(float));
    }
    p1 = poly1 + 9; p2 = poly2 + 9;
    for (i = 0; i < 8u; i++) { p1[0] += p1[-1]; p2[0] -= p2[-1]; p1--; p2--; }
    a[0] = 1.0f;
    p1 = poly1 + 2; p2 = poly2 + 2;
    for (i = 0; i < 8u; i++) { a[1 + i] = 0.5f * (*p1 + *p2); a[16 - i] = 0.5f * (*p1++ - *p2++); }
}
void sauti_usac_lpd_lpc_interpolate(const float *lsp_old, const float *lsp_new, float *a, unsigned nb_subfr) {
    const float factor = 1.0f / (float)nb_subfr;
    float plus = 0.5f * factor, lsp[16];
    unsigned k, i;
    for (k = 0; k < nb_subfr; k++) {
        const float minus = 1.0f - plus;
        for (i = 0; i < 16u; i++) lsp[i] = lsp_old[i] * minus + lsp_new[i] * plus;
        plus += factor;
        sauti_usac_lpd_lsp_to_lpc(lsp, a + 17u * k);
    }
    sauti_usac_lpd_lsp_to_lpc(lsp_new, a + 17u * nb_subfr);
}
void sauti_usac_lpd_lpc_tcx_set(const float *lsp_old, const float *lsp_new, float *a, unsigned nb_subfr) {
    const float inc = 1.0f / (float)nb_subfr, fnew = 0.5f - 0.5f * inc, fold = 1.0f - fnew;
    float lsp[16];
    unsigned i;
    for (i = 0; i < 16u; i++) lsp[i] = lsp_old[i] * fold + lsp_new[i] * fnew;
    sauti_usac_lpd_lsp_to_lpc(lsp, a);
    sauti_usac_lpd_lsp_to_lpc(lsp_old, a + 17);
    sauti_usac_lpd_lsp_to_lpc(lsp_new, a + 34);
}
