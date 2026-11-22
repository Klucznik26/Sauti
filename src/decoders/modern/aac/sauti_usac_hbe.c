/* USAC eSBR harmonic transposer (QMF based), see include/sauti_usac_hbe.h and docs/USAC_HBE_PLAN.md.  Written for Sauti after studying the processing chain of libxaac (Apache 2.0); only the data tables come from there
 * (sauti_usac_hbe_tables.c).  All arithmetic is in double precision, the buffers are rows of complex samples indexed [row][band] (the reference keeps interleaved float rows of 128 values). */
#include "sauti_usac_hbe.h"
#include "sauti_usac_hbe_tables.h"
#include <math.h>
#include <string.h>

typedef SautiUsacHbeCx cx;
#define MAX_ROWS (SAUTI_USAC_HBE_MAX_SLOTS / 2u + SAUTI_USAC_HBE_CONTEXT)         /* rows of the operating window */
#define OUT_ROWS (SAUTI_USAC_HBE_MAX_SLOTS + SAUTI_USAC_HBE_CARRY)
typedef struct {                                         /* the working set of one frame */
    cx in[MAX_ROWS][SAUTI_USAC_HBE_BANDS];
    cx out[OUT_ROWS][SAUTI_USAC_HBE_BANDS];
    cx norm[MAX_ROWS][SAUTI_USAC_HBE_BANDS];
    double tbuf[(SAUTI_USAC_HBE_MAX_SLOTS + 1u) * SAUTI_USAC_HBE_MAX_SYNTH];
    const SautiUsacHbe *h;
} Work;
#define PI 3.14159265358979323846

#define ZERO_ROW 6u                                      /* the column of the operating window the products are centred on */
#define WIN_LEN 13u                                      /* operating window: 12 columns of context + 1 */

static cx cmul(cx a, cx b) { cx r; r.re = a.re * b.re - a.im * b.im; r.im = a.re * b.im + a.im * b.re; return r; }
static cx cscale(cx a, double s) { cx r; r.re = a.re * s; r.im = a.im * s; return r; }
static double cmag2(cx a) { return a.re * a.re + a.im * a.im; }
static void cadd(cx *dst, cx a, double s) { dst->re += a.re * s; dst->im += a.im * s; }
static cx cpow_n(cx a, unsigned n) { cx r = a; unsigned i; for (i = 1; i < n; i++) r = cmul(r, a); return r; }   /* a^n, n >= 1, by repeated multiplication as in the reference */
static cx unit_norm(cx a, double exponent) { const double base = cmag2(a) + 1e-17; return cscale(a, pow(base, exponent)); }   /* a * (|a|^2 + 1e-17)^exponent */

static const float *window_for(unsigned len) {
    switch (len) {
    case 4: return sauti_usac_hbe_window;
    case 8: return sauti_usac_hbe_window + 40;
    case 12: return sauti_usac_hbe_window + 120;
    case 16: return sauti_usac_hbe_window + 240;
    case 20: return sauti_usac_hbe_window + 400;
    case 24: return sauti_usac_hbe_window + 600;
    case 32: return sauti_usac_hbe_window + 840;
    case 40: return sauti_usac_hbe_window + 1160;
    default: return 0;
    }
}

int sauti_usac_hbe_reinit(SautiUsacHbe *h, unsigned no_bins, const uint8_t *low, unsigned n_low, const uint8_t *high, unsigned n_high, unsigned upsamp4, unsigned ccfl768) {
    unsigned start, end, synth, patch, sfb = 0;
    int k_start;
    if (!h || !low || !high || no_bins != (upsamp4 ? 64u : 32u) || n_low < 1u || n_high < 1u) return 0;
    start = low[0]; end = low[n_low];
    if (start < 1u || start > 32u || end > 64u || end <= start) return 0;
    synth = 4u * ((start + 4u) / 8u + 1u);
    k_start = (int)sauti_usac_hbe_start_band_to_kl[start];
    if (upsamp4) { if (k_start + (int)synth > 16) k_start = 16 - (int)synth; }
    else if (ccfl768) { if (k_start + (int)synth > 24) k_start = 24 - (int)synth; }
    if (k_start < 0 || k_start + (int)synth > 32 || k_start % 2) return 0;
    if (!h->ready) memset(h, 0, sizeof *h);
    h->no_bins = no_bins; h->start_band = start; h->end_band = end; h->synth_size = synth; h->k_start = (unsigned)k_start; h->upsamp4 = upsamp4;
    memset(h->synth_buf, 0, sizeof h->synth_buf);
    memset(h->analy_buf, 0, sizeof h->analy_buf);
    memset(h->x_over, 0, sizeof h->x_over);
    {
        const unsigned stop_patch = upsamp4 ? SAUTI_USAC_HBE_PATCHES : 4u;
        int stretched = 0;
        h->max_stretch = 4u;
        for (patch = 1u; patch <= stop_patch; patch++) {
            while (sfb <= n_low && low[sfb] <= patch * start) sfb++;
            if (sfb <= n_low) {
                if (patch * start - low[sfb - 1u] <= 3u) h->x_over[patch - 1u] = low[sfb - 1u];
                else {
                    unsigned s2 = 0;
                    while (s2 <= n_high && high[s2] <= patch * start) s2++;
                    h->x_over[patch - 1u] = high[s2 - 1u];
                }
            } else {
                h->x_over[patch - 1u] = end;
                h->max_stretch = patch < 4u ? patch : 4u;
                stretched = 1;
                break;
            }
        }
        (void)stretched;
    }
    h->ready = 1u;
    return 1;
}

/* 2S real samples (the first S are the modulated subband samples, the rest zero) -> cos-modulated synthesis, one slot: S new time samples (appended at out) */
static void synth_slot(SautiUsacHbe *h, const cx *qrow, double *out) {
    const unsigned S = h->synth_size, ks = h->k_start;
    const float *ct = &sauti_usac_hbe_cos_trans_qmf[0][0] + (size_t)ks * 32u;
    double v[40], g[200], buf_new[40];
    double buffer[20u * SAUTI_USAC_HBE_MAX_SYNTH];
    const float *win = window_for(S);
    unsigned k, l, i, j;
    for (k = 0; k < 20u * S; k++) buffer[k] = (double)h->synth_buf[k];
    for (k = 0; k < S; k++) { v[k] = ct[2u * k] * qrow[ks + k].re + ct[2u * k + 1u] * qrow[ks + k].im; v[k + S] = 0.0; }
    for (l = 20u * S - 1u; l >= 2u * S; l--) buffer[l] = buffer[l - 2u * S];
    if (S == 20u) {
        const float *t = sauti_usac_hbe_synth_cos_20;
        for (l = 0; l < S + 1u; l++) {                    /* the reference's order of writes matters: buffer[S - l] overwrites buffer[l] for l = S */
            double a = 0.0;
            for (k = 0; k < S; k++) a += v[k] * t[k];
            buffer[l] = a; buffer[S - l] = a; t += S;
        }
        for (l = S + 1u; l < 2u * S - S / 2u; l++) {
            double a = 0.0;
            for (k = 0; k < S; k++) a += v[k] * t[k];
            buffer[l] = a; buffer[3u * S - l] = -a; t += S;
        }
        {
            double a = 0.0;
            for (k = 0; k < S; k++) a += v[k] * t[k];
            buffer[3u * S >> 1] = a;
        }
    } else {
        const float *tw = S == 4u ? sauti_usac_hbe_synth_cos_4 : S == 8u ? sauti_usac_hbe_synth_cos_8 : S == 12u ? sauti_usac_hbe_synth_cos_12 : sauti_usac_hbe_synth_cos_16;
        double cw[40], sw[40];
        for (k = 0; k < 2u * S; k++) { cw[k] = cos(2.0 * PI * (double)k / (double)(2u * S)); sw[k] = sin(2.0 * PI * (double)k / (double)(2u * S)); }
        for (k = 0; k < 2u * S; k++) {                    /* y[k] = sum v[t] exp(+2 pi j t k / 2S); buffer[(k + S/2) mod 2S] = Re(y[k] * (c + j s)) */
            double yr = 0.0, yi = 0.0;
            for (j = 0; j < S; j++) { const unsigned n = (j * k) % (2u * S); yr += v[j] * cw[n]; yi += v[j] * sw[n]; }
            buf_new[(k + S / 2u) % (2u * S)] = yr * tw[2u * k] - yi * tw[2u * k + 1u];
        }
        for (k = 0; k < 2u * S; k++) buffer[k] = buf_new[k];
    }
    for (i = 0; i < 5u; i++) {
        memcpy(&g[(2u * i) * S], &buffer[(4u * i) * S], sizeof(double) * S);
        memcpy(&g[(2u * i + 1u) * S], &buffer[(4u * i + 3u) * S], sizeof(double) * S);
    }
    for (i = 0; i < S; i++) {
        double a = 0.0;
        for (j = 0; j < 10u; j++) a += g[S * j + i] * (double)win[S * j + i];
        out[i] = a;
    }
    for (k = 0; k < 20u * S; k++) h->synth_buf[k] = (float)buffer[k];
}

/* 2S real time samples of one analysis step (reversed into the history) -> 2S complex bands written at row[2 * k_start ...] */
static void analysis_column(SautiUsacHbe *h, const double *inp, cx *row) {
    const unsigned S = h->synth_size, A = 2u * S, N = 10u * A;
    const float *win = window_for(A);
    double wo[640], u[160], x[640];
    unsigned i, j, k, l;
    cx *dst = row + 2u * h->k_start;
    for (i = 0; i < N; i++) x[i] = (double)h->analy_buf[i];
    for (i = N - 1u; i >= A; i--) x[i] = x[i - A];
    for (i = 0; i < A; i++) x[i] = inp[A - 1u - i];
    for (i = 0; i < N; i++) { wo[i] = x[i] * (double)win[i]; h->analy_buf[i] = (float)x[i]; }
    for (i = 0; i < 2u * A; i++) { double a = 0.0; for (j = 0; j < 5u; j++) a += wo[i + j * 2u * A]; u[i] = a; }
    if (A == 40u) {
        const float *t = sauti_usac_hbe_analy_40;
        for (i = 1; i < A; i++) { const double t1 = u[i] + u[2u * A - i], t2 = u[i] - u[2u * A - i]; u[i] = t1; u[2u * A - i] = t2; }
        for (k = 0; k < A; k++) {
            double ar = u[A], ai = (k & 1u) ? u[0] : -u[0];
            for (l = 1; l < A; l++) { ar += u[l] * t[2u * l]; ai += u[2u * A - l] * t[2u * l + 1u]; }
            t += 2u * A;
            dst[k].re = ar; dst[k].im = ai;
        }
    } else {
        const float *t = A == 8u ? sauti_usac_hbe_analy_8 : A == 16u ? sauti_usac_hbe_analy_16 : A == 24u ? sauti_usac_hbe_analy_24 : sauti_usac_hbe_analy_32;
        double cw[80], sw[80];
        for (k = 0; k < 2u * A; k++) { cw[k] = cos(2.0 * PI * (double)k / (double)(2u * A)); sw[k] = sin(2.0 * PI * (double)k / (double)(2u * A)); }
        for (k = 0; k < A; k++) {                         /* complex bin k of the 2A point transform of u[n] * (c_n + j s_n), exponent +, bins 0..A-1, then the reference's -j / +j rotation by parity */
            double yr = 0.0, yi = 0.0;
            for (j = 0; j < 2u * A; j++) {
                const unsigned n = (j * k) % (2u * A);
                const double c = cw[n], s = sw[n], xr = u[j] * t[2u * j], xi = u[j] * t[2u * j + 1u];
                yr += xr * c - xi * s; yi += xr * s + xi * c;
            }
            if ((k & 1u) == 0) { dst[k].re = yi; dst[k].im = -yr; } else { dst[k].re = -yi; dst[k].im = yr; }
        }
    }
}

/* normalised copy of the operating buffer for the bands [from, to] (all rows): x * |x|^-e where e = 1/2 (2nd order), 3/4 (4th order) */
static void norm_buffer(Work *w, unsigned from, unsigned to, double exponent) {
    unsigned b, r;
    for (b = from; b <= to && b < SAUTI_USAC_HBE_BANDS; b++)
        for (r = 0; r < w->h->no_bins / 2u + SAUTI_USAC_HBE_CONTEXT; r++) w->norm[r][b] = unit_norm(w->in[r][b], -exponent * 0.5);
}

static cx interp2(const float *sel, cx a, cx b) {        /* sel[0..3] applied to a, sel[4..7] to b (2x2 real matrices on (re, im)) */
    cx r;
    r.re = sel[0] * a.re + sel[1] * a.im + sel[4] * b.re + sel[5] * b.im;
    r.im = sel[2] * a.re + sel[3] * a.im + sel[6] * b.re + sel[7] * b.im;
    return r;
}

/* order 2, plain product of the normalised columns */
static void prod2(Work *w, unsigned from, unsigned to) {
    unsigned b, i, k;
    norm_buffer(w, from, w->h->x_over[1], 0.5);
    for (b = from; b < to; b++)
        for (i = 0; i < w->h->no_bins / 2u; i++) {
            const cx x0 = w->norm[ZERO_ROW + i][b];
            for (k = 0; k < 10u; k++) cadd(&w->out[1u + 2u * i + k][b], cmul(w->norm[1u + i + k][b], x0), 0.3333333);
        }
}

/* the cross product of order 2 (pitch dependent) added to the plain product of one band and column */
static void xprod2_term(Work *w, unsigned b, unsigned i, float p, const float *cs) {
    const double tf = (2.0 * b + 1.0 - (double)p) * 0.5;                   /* the reference's index arithmetic, including the float rounding of p */
    const int n1 = (int)tf, n2 = (int)(tf + (double)p);
    const cx *row = w->in[i + ZERO_ROW];
    double m1, m2, mn, mz;
    cx xz, y0, y1, t;
    if (n1 < 0 || n2 >= 64) return;
    mz = cmag2(row[b]); m1 = cmag2(row[n1]); m2 = cmag2(row[n2]); mn = m1 < m2 ? m1 : m2;
    if (!(mn > 0.0) || !(mn > mz)) return;
    xz = unit_norm(row[n1], -0.25);
    y0 = unit_norm(w->in[i + ZERO_ROW - 1u][n2], -0.25);
    y1 = unit_norm(w->in[i + ZERO_ROW][n2], -0.25);
    t = cmul(y0, xz);
    { cx r; r.re = cs[0] * t.re - cs[1] * t.im; r.im = cs[0] * t.im + cs[1] * t.re; cadd(&w->out[2u * i + ZERO_ROW - 1u][b], r, 1.666666667); }
    t = cmul(y1, xz);
    cadd(&w->out[2u * i + ZERO_ROW][b], t, 1.666666667);
}


static void cross_proc3(Work *w, unsigned b, unsigned i, float p, unsigned pitch_idx) {
    const cx *row = w->in[i + ZERO_ROW];
    const unsigned inp = (2u * b) / 3u;
    double maxm = 0.0, mz = cmag2(row[inp]);
    int tr, max_tr = 0, max_n1 = 0, max_n2 = 0;
    for (tr = 1; tr < 3; tr++) {
        const double tf = (double)((2.0f * (float)b + 1.0f) - (float)tr * p) * 0.3333334;
        const int n1 = (int)tf, n2 = (int)(tf + (double)p);
        double m1, m2, mn;
        if (n1 < 0 || n2 >= 64) { m1 = m2 = 0.0; } else { m1 = cmag2(row[n1]); m2 = cmag2(row[n2]); }
        mn = m1 < m2 ? m1 : m2;
        if (mn > maxm) { maxm = mn; max_tr = tr; max_n1 = n1; max_n2 = n2; }
    }
    if (maxm > mz && max_n1 >= 0 && max_n2 < 64) {
        cx xz, y[2], o[2], c0, c1, t0, t1;
        double d1, d2, ct, st;
        unsigned mid = 3u - (unsigned)max_tr, ypow = (unsigned)max_tr, idx, src_n, k;
        if (max_tr == 1) { d1 = 0.0; d2 = 1.5; xz = row[max_n1]; src_n = (unsigned)max_n2; }
        else { d1 = 1.5; d2 = 0.0; mid = (unsigned)max_tr; ypow = 3u - (unsigned)max_tr; xz = row[max_n2]; src_n = (unsigned)max_n1; }
        idx = ((src_n & 3u) + 1u) & 3u;
        c0.re = sauti_usac_hbe_interp_coeff[idx][0]; c0.im = sauti_usac_hbe_interp_coeff[idx][1]; c1.re = c0.re; c1.im = -c0.im;
        y[1] = row[src_n];
        t0 = w->in[i + ZERO_ROW - 2u][src_n]; t1 = w->in[i + ZERO_ROW - 1u][src_n];
        y[0] = cmul(c1, t0); { const cx q = cmul(c0, t1); y[0].re += q.re; y[0].im += q.im; }
        xz = unit_norm(xz, -1.0 / 3.0); y[0] = unit_norm(y[0], -1.0 / 3.0); y[1] = unit_norm(y[1], -1.0 / 3.0);
        if (mid > 1u) xz = cpow_n(xz, mid);
        for (k = 0; k < 2u; k++) { if (ypow > 1u) y[k] = cpow_n(y[k], ypow); o[k] = cmul(y[k], xz); }
        ct = sauti_usac_hbe_xprod_cos_3[2u * pitch_idx]; st = sauti_usac_hbe_xprod_cos_3[2u * pitch_idx + 1u];
        if (d2 < d1) st = -st;
        { cx r; r.re = ct * o[0].re - st * o[0].im; r.im = ct * o[0].im + st * o[0].re; o[0] = r; }
        for (k = 0; k < 2u; k++) cadd(&w->out[2u * i + ZERO_ROW - 1u + k][b], o[k], 1.8856);
    }
}

static void prod3(Work *w, unsigned from, unsigned to, int with_cross, float p, unsigned pitch_idx) {
    unsigned b, i, k, m;
    for (b = from; b < to; b++) {
        const unsigned inp = (2u * b) / 3u, rem = 2u * b - 3u * inp;
        const float *sel = sauti_usac_hbe_sel_case[(inp + 1u) & 3u], *sel1 = sauti_usac_hbe_sel_case[((inp + 1u) & 3u) + 1u];
        for (i = 0; i < w->h->no_bins / 2u; i++) {
            cx vx[8], vc[8];
            if (rem == 0u || rem == 1u) {
                for (m = 0; m < 4u; m++) {
                    const unsigned r = 3u * m + i;
                    cx mid;
                    vx[2u * m] = unit_norm(w->in[r][inp], -1.0 / 3.0);
                    mid = interp2(sel, w->in[r + 2u][inp], w->in[r + 1u][inp]);
                    vx[2u * m + 1u] = unit_norm(cscale(mid, 0.3984033437), -1.0 / 3.0);
                }
                {
                    const cx z = cmul(vx[4], vx[4]);
                    for (k = 0; k < 8u; k++) cadd(&w->out[2u + 2u * i + k][b], cmul(vx[k], z), 0.4714045);
                }
            } else {
                for (m = 0; m < 4u; m++) {
                    const unsigned r = 3u * m + i;
                    cx a, c;
                    vx[2u * m] = unit_norm(w->in[r][inp + 1u], -1.0 / 3.0);
                    vc[2u * m] = unit_norm(w->in[r][inp], -1.0 / 3.0);
                    c = interp2(sel, w->in[r + 2u][inp], w->in[r + 1u][inp]);
                    a = interp2(sel1, w->in[r + 2u][inp + 1u], w->in[r + 1u][inp + 1u]);
                    vc[2u * m + 1u] = unit_norm(cscale(c, 0.3984033437), -1.0 / 3.0);
                    vx[2u * m + 1u] = unit_norm(cscale(a, 0.3984033437), -1.0 / 3.0);
                }
                {
                    const cx zc = cmul(vc[4], vc[4]), zv = cmul(vx[4], vx[4]);
                    for (k = 0; k < 8u; k++) {
                        cx s = cmul(vx[k], zc), s2 = cmul(vc[k], zv);
                        s.re += s2.re; s.im += s2.im;
                        cadd(&w->out[2u + 2u * i + k][b], s, 0.23570225);
                    }
                }
            }
            if (with_cross) cross_proc3(w, b, i, p, pitch_idx);
        }
    }
}

static void cross_proc4(Work *w, unsigned b, unsigned i, float p, unsigned pitch_idx) {
    const cx *row = w->in[i + ZERO_ROW];
    const unsigned inp = b >> 1;
    double maxm = 0.0, mz = cmag2(row[inp]);
    int tr, max_tr = 0, max_n1 = 0, max_n2 = 0;
    for (tr = 1; tr < 4; tr++) {
        const double tf = (2.0 * b + 1.0 - (double)((float)tr * p)) * 0.25;
        const int n1 = (int)tf, n2 = (int)(tf + (double)p);
        double m1, m2, mn;
        if (n1 < 0 || n2 >= 64) { m1 = m2 = 0.0; } else { m1 = cmag2(row[n1]); m2 = cmag2(row[n2]); }
        mn = m1 < m2 ? m1 : m2;
        if (mn > maxm) { maxm = mn; max_tr = tr; max_n1 = n1; max_n2 = n2; }
    }
    if (maxm > mz && max_n1 >= 0 && max_n2 < 64) {
        cx xz, y[2], o[2];
        double d1, d2, ct, st;
        unsigned mid = 4u - (unsigned)max_tr, ypow = (unsigned)max_tr, k;
        int rows[2], src_n;
        if (max_tr == 1) { d1 = 0.0; d2 = 2.0; xz = row[max_n1]; src_n = max_n2; rows[0] = -2; rows[1] = 0; }
        else if (max_tr == 2) { d1 = 0.0; d2 = 1.0; xz = row[max_n1]; src_n = max_n2; rows[0] = -1; rows[1] = 0; }
        else { d1 = 2.0; d2 = 0.0; mid = (unsigned)max_tr; ypow = 4u - (unsigned)max_tr; xz = row[max_n2]; src_n = max_n1; rows[0] = -2; rows[1] = 0; }
        for (k = 0; k < 2u; k++) y[k] = w->in[(int)(i + ZERO_ROW) + rows[k]][src_n];
        xz = unit_norm(xz, -0.375); y[0] = unit_norm(y[0], -0.375); y[1] = unit_norm(y[1], -0.375);
        if (mid > 1u) xz = cpow_n(xz, mid);
        for (k = 0; k < 2u; k++) { if (ypow > 1u) y[k] = cpow_n(y[k], ypow); o[k] = cmul(y[k], xz); }
        if (d2 == 1.0) { ct = sauti_usac_hbe_xprod_cos_4_1[2u * pitch_idx]; st = sauti_usac_hbe_xprod_cos_4_1[2u * pitch_idx + 1u]; }
        else { ct = sauti_usac_hbe_xprod_cos_4[2u * pitch_idx]; st = sauti_usac_hbe_xprod_cos_4[2u * pitch_idx + 1u]; if (d2 < d1) st = -st; }
        { cx r; r.re = ct * o[0].re - st * o[0].im; r.im = ct * o[0].im + st * o[0].re; o[0] = r; }
        for (k = 0; k < 2u; k++) cadd(&w->out[2u * i + ZERO_ROW - 1u + k][b], o[k], 2.0);
    }
}

static void prod4(Work *w, unsigned from, unsigned to, int with_cross, float p, unsigned pitch_idx) {
    unsigned b, i, k;
    norm_buffer(w, (from >> 1) - 1u, w->h->x_over[3], 0.75);
    for (b = from; b < to; b++) {
        const unsigned inp = b >> 1, ip = (b & 1u) ? inp + 1u : inp - 1u;
        for (i = 0; i < w->h->no_bins / 2u; i++) {
            const cx x0 = w->norm[ZERO_ROW + i][inp], x3 = cmul(x0, cmul(x0, x0));
            for (k = 0; k < 6u; k++) cadd(&w->out[3u + 2u * i + k][b], cmul(w->norm[i + 2u * k][ip], x3), 0.6666667);
            if (with_cross) cross_proc4(w, b, i, p, pitch_idx);
        }
    }
}

static int post_process(Work *w, unsigned pitch_in_bins) {
    const float p = w->h->upsamp4 ? (float)(pitch_in_bins * 0.04166666666666) : (float)(pitch_in_bins * 0.08333333333333);   /* float: 12 * 0.0833.. rounds to exactly 1.0 */
    const unsigned pitch_idx = pitch_in_bins + (w->h->upsamp4 ? 128u : 0u);
    if (pitch_in_bins > 127u) return 0;
    if (p < 1.0f) {
        if (2u <= w->h->max_stretch) prod2(w, w->h->x_over[0], w->h->x_over[1]);
        if (3u <= w->h->max_stretch) prod3(w, w->h->x_over[1], w->h->x_over[2], 0, p, pitch_idx);
        if (4u <= w->h->max_stretch) { if (w->h->x_over[2] <= 1u) return 0; prod4(w, w->h->x_over[2], w->h->x_over[3], 0, p, pitch_idx); }
    } else {
        if (2u <= w->h->max_stretch) {
            const float *cs = &sauti_usac_hbe_xprod_cos_2[2u * pitch_idx];
            unsigned b, i;
            prod2(w, w->h->x_over[0], w->h->x_over[1]);
            for (b = w->h->x_over[0]; b < w->h->x_over[1]; b++) for (i = 0; i < w->h->no_bins / 2u; i++) xprod2_term(w, b, i, p, cs);
        }
        if (3u <= w->h->max_stretch) prod3(w, w->h->x_over[1], w->h->x_over[2], 1, p, pitch_idx);
        if (4u <= w->h->max_stretch) { if (w->h->x_over[2] <= 1u) return 0; prod4(w, w->h->x_over[2], w->h->x_over[3], 1, p, pitch_idx); }
    }
    return 1;
}

/* eSBR 4:1: the orders above the fourth are not transposed, the patches beyond them repeat the last transposed patch (the band pattern is mirrored the way the reference's spectral replication does it:
 * every copy starts below the last source band and runs downwards, and a copy that would land on a band of the other parity is one band shorter or starts one band lower) */
void sauti_usac_hbe_replicate(const SautiUsacHbe *h, SautiUsacHbeCx *pv) {
    const unsigned NB = h->no_bins, ms = h->max_stretch;
    unsigned num_patches = 0, patch, i, col;
    for (i = 1; i < SAUTI_USAC_HBE_PATCHES; i++) if (h->x_over[i]) num_patches++;
    if (ms < 2u) return;
    for (patch = ms - 1u; patch < num_patches; patch++) {
        int patch_bands = (int)h->x_over[patch + 1u] - (int)h->x_over[patch];
        int target = (int)h->x_over[patch];
        const int source_bands = (int)h->x_over[ms - 1u] - (int)h->x_over[ms - 2u];
        while (patch_bands > 0) {
            int num = source_bands, start_band = (int)h->x_over[ms - 1u] - 1;
            if (target + num >= (int)h->x_over[patch + 1u]) num = (int)h->x_over[patch + 1u] - target;
            if ((((target + num - 1) & 1) + (((int)h->x_over[ms - 1u] - 1) & 1)) & 1) {
                if (num == source_bands) num--;
                else start_band--;
            }
            if (num <= 0) break;
            for (col = 0; col < NB; col++) {
                int band = target + num - 1;
                if (band >= 64) band = 63;
                if ((int)h->x_over[patch + 1u] <= band) band = (int)h->x_over[patch + 1u] - 1;
                for (i = 0; i < (unsigned)num; i++, band--) {
                    if (band < 0 || start_band - (int)i < 0) break;
                    pv[(size_t)col * 64u + (unsigned)band] = pv[(size_t)col * 64u + (unsigned)(start_band - (int)i)];
                }
            }
            target += num; patch_bands -= num;
        }
    }
}

int sauti_usac_hbe_process(SautiUsacHbe *h, const SautiUsacHbeCx *q, unsigned pitch_in_bins, SautiUsacHbeCx *pv) {
    const unsigned NB = h->no_bins, S = h->synth_size;
    Work w;
    unsigned i, b, k;
    if (!h->ready || !q || !pv) return 0;
    memset(pv, 0, sizeof(cx) * (size_t)NB * SAUTI_USAC_HBE_BANDS);
    memset(&w, 0, sizeof w);
    w.h = h;
    for (k = 0; k < S; k++) w.tbuf[k] = (double)h->time_tail[k];
    for (i = 0; i < NB; i++) synth_slot(h, q + (size_t)i * 64u, w.tbuf + (size_t)(i + 1u) * S);
    for (k = 0; k < S; k++) h->time_tail[k] = (float)w.tbuf[(size_t)NB * S + k];
    for (i = 0; i < SAUTI_USAC_HBE_CONTEXT; i++) for (b = 0; b < SAUTI_USAC_HBE_BANDS; b++) { w.in[i][b].re = (double)h->context[i][b][0]; w.in[i][b].im = (double)h->context[i][b][1]; }
    for (i = 0; i < NB / 2u; i++) analysis_column(h, w.tbuf + (size_t)i * 2u * S + 1u, w.in[i + SAUTI_USAC_HBE_CONTEXT]);
    for (i = 0; i < SAUTI_USAC_HBE_CONTEXT; i++) for (b = 0; b < SAUTI_USAC_HBE_BANDS; b++) { h->context[i][b][0] = (float)w.in[i + NB / 2u][b].re; h->context[i][b][1] = (float)w.in[i + NB / 2u][b].im; }
    for (i = 0; i < SAUTI_USAC_HBE_CARRY; i++) for (b = 0; b < SAUTI_USAC_HBE_BANDS; b++) { w.out[i][b].re = (double)h->carry[i][b][0]; w.out[i][b].im = (double)h->carry[i][b][1]; }
    if (!post_process(&w, pitch_in_bins)) return 0;
    for (i = 0; i < NB; i++)
        for (b = h->start_band; b < h->end_band; b++) {
            const double c = sauti_usac_hbe_pv_cos[b], s = sauti_usac_hbe_pv_sin[b];
            pv[(size_t)i * 64u + b].re = w.out[i][b].re * c - w.out[i][b].im * s;
            pv[(size_t)i * 64u + b].im = w.out[i][b].re * s + w.out[i][b].im * c;
        }
    for (i = 0; i < SAUTI_USAC_HBE_CARRY; i++) for (b = 0; b < SAUTI_USAC_HBE_BANDS; b++) { h->carry[i][b][0] = (float)w.out[NB + i][b].re; h->carry[i][b][1] = (float)w.out[NB + i][b].im; }
    if (h->upsamp4) sauti_usac_hbe_replicate(h, pv);
    return 1;
}
