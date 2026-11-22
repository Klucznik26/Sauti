/* USAC MPEG Surround 212, signal path: see include/sauti_usac_mps_dec.h.  Written for Sauti after studying libxaac's ixheaacd_mps_hybrid_filt.c, ixheaacd_mps_decorr.c, ixheaacd_mps_pre_mix.c and ixheaacd_mps_dec.c
 * (Apache 2.0); the tables come from there (sauti_usac_mps_tables.c).  Arithmetic in double, state in float. */
#include "sauti_usac_mps_dec.h"
#include "sauti_usac_mps_tables.h"
#include <math.h>
#include <string.h>

typedef SautiAacSbrComplex cxf;
#define NB SAUTI_USAC_MPS_BANDS
#define HYB SAUTI_USAC_MPS_HYBRID
#define MAXS 72u                                         /* time slots per frame (the reference's MAX_TIME_SLOTS) */
/* energies in this library are those of the reference divided by 2^30 (its QMF samples carry the 16 bit scale) */
#define ABS_THR 1.0e-9
#define DECOR_ALPHA 0.8
#define DECOR_GAMMA 1.5

static const uint8_t *band_map(unsigned bands) {
    switch (bands) {
    case 28: return sauti_usac_mps_band_map_28; case 20: return sauti_usac_mps_band_map_20; case 14: return sauti_usac_mps_band_map_14; case 10: return sauti_usac_mps_band_map_10;
    case 7: return sauti_usac_mps_band_map_7; case 5: return sauti_usac_mps_band_map_5; case 4: return sauti_usac_mps_band_map_4; default: return 0;
    }
}
static const float *const lat_num[4] = { sauti_usac_mps_decor0_num, sauti_usac_mps_decor1_num, sauti_usac_mps_decor2_num, sauti_usac_mps_decor3_num };
static const float *const lat_den[4] = { sauti_usac_mps_decor0_den, sauti_usac_mps_decor1_den, sauti_usac_mps_decor2_den, sauti_usac_mps_decor3_den };
static const unsigned lat_len[4] = { 11, 9, 4, 3 };
static const unsigned decor_delay[4] = { 11, 10, 5, 2 };
static const unsigned split_freq[3][4] = { { 3, 15, 24, 65 }, { 3, 50, 65, 65 }, { 0, 15, 65, 65 } };

/* the 8-band split of QMF band 0 (13 tap filter, folded to 6 hybrid bands): out[6][slots] from x[slot - 12 .. slot] */
static void hyb_split8(const cxf *x, unsigned slots, cxf out[6][MAXS]) {
    unsigned i, n, q;
    for (i = 0; i < slots; i++) {
        double x0r[13], x0i[13], x1r[8], x1i[8], ar[8], ai[8];
        cxf o[8];
        for (n = 0; n < 13; n++) { const double c = sauti_usac_mps_hyb8[12u - n]; x0r[n] = c * x[n + i].re; x0i[n] = c * x[n + i].im; }
        x1r[0] = x0r[6]; x1i[0] = x0i[6]; x1r[1] = x0r[7]; x1i[1] = x0i[7];
        for (n = 2; n < 7; n++) { x1r[n] = x0r[n + 6] - x0r[n - 2]; x1i[n] = x0i[n + 6] - x0i[n - 2]; }
        x1r[7] = -x0r[5]; x1i[7] = -x0i[5];
        ar[0] = x1r[0]; ar[1] = x1r[1] - x1r[7]; ar[2] = x1r[2] - x1r[6]; ar[3] = x1r[3] - x1r[5];
        ar[4] = x1i[1] + x1i[7]; ar[5] = x1i[2] + x1i[6]; ar[6] = x1i[3] + x1i[5]; ar[7] = x1i[4];
        ai[0] = x1i[0]; ai[1] = x1i[1] - x1i[7]; ai[2] = x1i[2] - x1i[6]; ai[3] = x1i[3] - x1i[5];
        ai[4] = x1r[1] + x1r[7]; ai[5] = x1r[2] + x1r[6]; ai[6] = x1r[3] + x1r[5]; ai[7] = x1r[4];
        for (q = 0; q < 4; q++) {
            double rl = ar[0], rh, il = ai[0], ih;
            rl += ar[1] * sauti_usac_mps_hyb_cosine[q][1] + ar[2] * sauti_usac_mps_hyb_cosine[q][2] + ar[3] * sauti_usac_mps_hyb_cosine[q][3];
            rh = rl;
            rl -= ar[4] * sauti_usac_mps_hyb_sine[q][1] + ar[5] * sauti_usac_mps_hyb_sine[q][2] + ar[6] * sauti_usac_mps_hyb_sine[q][3] + ar[7] * sauti_usac_mps_hyb_sine[q][4];
            rh = rh - (rl - rh);
            il += ai[1] * sauti_usac_mps_hyb_cosine[q][1] + ai[2] * sauti_usac_mps_hyb_cosine[q][2] + ai[3] * sauti_usac_mps_hyb_cosine[q][3];
            ih = il;
            il += ai[4] * sauti_usac_mps_hyb_sine[q][1] + ai[5] * sauti_usac_mps_hyb_sine[q][2] + ai[6] * sauti_usac_mps_hyb_sine[q][3] + ai[7] * sauti_usac_mps_hyb_sine[q][4];
            ih = ih - (il - ih);
            o[q].re = (float)rl; o[q].im = (float)il; o[7u - q].re = (float)rh; o[7u - q].im = (float)ih;
        }
        out[0][i] = o[6]; out[1][i] = o[7];
        out[2][i] = o[0]; out[3][i] = o[1];
        out[4][i].re = o[2].re + o[5].re; out[4][i].im = o[2].im + o[5].im;
        out[5][i].re = o[3].re + o[4].re; out[5][i].im = o[3].im + o[4].im;
    }
}
/* the 2-band split of QMF bands 1 and 2 */
static void hyb_split2(const cxf *x, unsigned slots, cxf out[2][MAXS]) {
    unsigned i, n;
    for (i = 0; i < slots; i++) {
        double r[7], im[7];
        for (n = 1; n < 6; n += 2) { const double c = sauti_usac_mps_hyb2[12u - n]; r[n] = c * (x[n + i].re + x[12u - n + i].re); im[n] = c * (x[n + i].im + x[12u - n + i].im); }
        r[6] = sauti_usac_mps_hyb2[6] * x[6u + i].re; im[6] = sauti_usac_mps_hyb2[6] * x[6u + i].im;
        r[1] = r[1] + r[3] + r[5]; im[1] = im[1] + im[3] + im[5];
        out[0][i].re = (float)(r[6] + r[1]); out[0][i].im = (float)(im[6] + im[1]);
        out[1][i].re = (float)(r[6] - r[1]); out[1][i].im = (float)(im[6] - im[1]);
    }
}

/* upmix gains of a parameter set: rows 0 / 1 = left / right output, g[0] for the direct signal, g[1] for the decorrelated one */
static void set_gains(unsigned prediction, int cld_idx, int icc_idx, float res[2], float dec[2]) {
    const unsigned c = (unsigned)(cld_idx + 15), ic = (unsigned)icc_idx & 7u;
    if (!prediction) {
        const double cl = sauti_usac_mps_c_l[c], cr = sauti_usac_mps_c_l[30u - c];
        res[0] = (float)((double)sauti_usac_mps_cos[ic][c] * cl);  res[1] = (float)((double)sauti_usac_mps_cos[ic][30u - c] * cr);
        dec[0] = (float)((double)sauti_usac_mps_sin[ic][c] * cl);  dec[1] = (float)(-(double)sauti_usac_mps_sin[ic][30u - c] * cr);
    } else {
        const double w = sauti_usac_mps_weight[0][ic][c], rw = sauti_usac_mps_re_weight[0][ic][c], beta = sauti_usac_mps_beta[0][ic][c];
        res[0] = (float)(w - rw); res[1] = (float)(w + rw);
        dec[0] = (float)beta; dec[1] = (float)-beta;
    }
}

int sauti_usac_mps212_apply(SautiUsacMpsDec *d, const SautiUsacMpsConfig *cfg, const SautiUsacMpsFrame *f, unsigned band_count, unsigned slots,
                            const SautiAacSbrComplex *in, SautiAacSbrComplex *out0, SautiAacSbrComplex *out1) {
    cxf hyb[MAXS][HYB], diff[MAXS][HYB], mixed[2][MAXS][HYB];                 /* about 160 KiB of scratch on the stack */
    cxf lf_in[3][12 + MAXS], split8[6][MAXS], split2[2][MAXS], split2b[2][MAXS];
    float resg[SAUTI_USAC_MPS_SETS][NB][2], decg[SAUTI_USAC_MPS_SETS][NB][2];
    unsigned slot_diff[SAUTI_USAC_MPS_SETS];
    const uint8_t *map, *map28 = sauti_usac_mps_band_map_28;
    const unsigned *split;
    unsigned hyb_count, i, k, pb, n, ps, row;
    const double gain = cfg ? sauti_usac_mps_clip_gain[cfg->fixed_gain & 7u] : 1.0;
    if (!d || !cfg || !f || !in || !out0 || !out1 || slots == 0 || slots > MAXS || band_count < 4u || band_count > 64u || cfg->fixed_gain > 7u || cfg->decorr > 2u) return 0;
    if (cfg->phase_coding || cfg->residual_bands || f->num_sets == 0 || f->num_sets > SAUTI_USAC_MPS_SETS || cfg->freq_res == 0 || cfg->freq_res > 7u || cfg->temp_shape == 1u || cfg->temp_shape == 3u) return 0;
    map = band_map(f->param_bands);
    if (!map) return 0;
    split = split_freq[cfg->decorr];
    hyb_count = band_count - 3u + 10u;
    for (i = 0; i < f->num_sets; i++) {
        const unsigned prev = i ? f->param_slot[i - 1u] : 0u;
        if (f->param_slot[i] >= slots || (i && f->param_slot[i] <= prev)) return 0;
        slot_diff[i] = i ? f->param_slot[i] - prev : f->param_slot[0] + 1u;
    }
    if (f->param_slot[f->num_sets - 1u] != slots - 1u) return 0;
    /* 1. hybrid analysis of the (gain scaled) downmix */
    for (k = 0; k < 3u; k++) {
        memcpy(lf_in[k], d->lf[k], 12u * sizeof(cxf));
        for (i = 0; i < slots; i++) { lf_in[k][12u + i].re = (float)(gain * in[(size_t)i * 64u + k].re); lf_in[k][12u + i].im = (float)(gain * in[(size_t)i * 64u + k].im); }
    }
    hyb_split8(lf_in[0], slots, split8);
    hyb_split2(lf_in[1], slots, split2);
    hyb_split2(lf_in[2], slots, split2b);
    memset(hyb, 0, sizeof hyb);
    for (i = 0; i < slots; i++) {
        for (k = 0; k < 6u; k++) hyb[i][k] = split8[k][i];
        hyb[i][6] = split2[1][i]; hyb[i][7] = split2[0][i];
        hyb[i][8] = split2b[0][i]; hyb[i][9] = split2b[1][i];
        for (k = 3; k < band_count; k++) { hyb[i][7u + k].re = (float)(gain * in[(size_t)i * 64u + k].re); hyb[i][7u + k].im = (float)(gain * in[(size_t)i * 64u + k].im); }
    }
    for (k = 0; k < 3u; k++) memcpy(d->lf[k], lf_in[k] + slots, 12u * sizeof(cxf));
    /* 2. decorrelator: per hybrid band a delay and an all-pass filter, then the energy of every parameter band (of the 28 band map) is brought back towards that of the input */
    memset(diff, 0, sizeof diff);
    for (k = 0; k < hyb_count; k++) {
        unsigned rb = 0, delay, len;
        cxf buf[SAUTI_USAC_MPS_DELAY_MAX + MAXS];
        while (rb < 3u && sauti_usac_mps_hybrid_to_qmf[k] >= split[rb] - 1u) rb++;
        delay = decor_delay[rb]; len = lat_len[rb];
        for (n = 0; n < delay; n++) buf[n] = d->delay[k][n];
        for (n = 0; n < slots; n++) buf[delay + n] = hyb[n][k];
        for (n = 0; n < slots; n++) {
            double sr = d->ap_state[k][0].re, si = d->ap_state[k][0].im, outr = sr + (double)buf[n].re * lat_num[rb][0], outi = si + (double)buf[n].im * lat_num[rb][0];
            unsigned j;
            for (j = 1; j < len; j++) {
                d->ap_state[k][j - 1u].re = (float)((double)d->ap_state[k][j].re + lat_num[rb][j] * (double)buf[n].re - lat_den[rb][j] * outr);
                d->ap_state[k][j - 1u].im = (float)((double)d->ap_state[k][j].im + lat_num[rb][j] * (double)buf[n].im - lat_den[rb][j] * outi);
            }
            diff[n][k].re = (float)outr; diff[n][k].im = (float)outi;
        }
        for (n = 0; n < delay; n++) d->delay[k][n] = buf[slots + n];
    }
    for (n = 0; n < slots; n++) {
        double in_e[NB], out_e[NB], g[NB];
        memset(in_e, 0, sizeof in_e); memset(out_e, 0, sizeof out_e);
        for (k = 0; k < hyb_count; k++) {
            in_e[map28[k]] += (double)hyb[n][k].re * hyb[n][k].re + (double)hyb[n][k].im * hyb[n][k].im;
            out_e[map28[k]] += (double)diff[n][k].re * diff[n][k].re + (double)diff[n][k].im * diff[n][k].im;
        }
        for (pb = 0; pb < NB; pb++) {
            d->smooth_in[pb] = (float)(d->smooth_in[pb] * DECOR_ALPHA + in_e[pb] * (1.0 - DECOR_ALPHA));
            d->smooth_out[pb] = (float)(d->smooth_out[pb] * DECOR_ALPHA + out_e[pb] * (1.0 - DECOR_ALPHA));
            g[pb] = 1.0;
            if (d->smooth_out[pb] > d->smooth_in[pb] * DECOR_GAMMA) g[pb] = sqrt(d->smooth_in[pb] * DECOR_GAMMA / (d->smooth_out[pb] + ABS_THR));
            if (d->smooth_in[pb] > d->smooth_out[pb] * DECOR_GAMMA) { const double v = sqrt(d->smooth_in[pb] / (DECOR_GAMMA * d->smooth_out[pb] + ABS_THR)); g[pb] = v < 2.0 ? v : 2.0; }
        }
        for (k = 0; k < hyb_count; k++) { diff[n][k].re = (float)(diff[n][k].re * g[map28[k]]); diff[n][k].im = (float)(diff[n][k].im * g[map28[k]]); }
    }
    /* 3. upmix gains of every parameter set, interpolated slot by slot, and the mix of the direct and the decorrelated signal */
    for (ps = 0; ps < f->num_sets; ps++)
        for (pb = 0; pb < f->param_bands; pb++) {
            float rs[2], dc[2];
            set_gains(cfg->residual, f->cld_idx[ps][pb], f->icc_idx[ps][pb], rs, dc);
            for (row = 0; row < 2u; row++) { resg[ps][pb][row] = rs[row]; decg[ps][pb][row] = dc[row]; }
        }
    memset(mixed, 0, sizeof mixed);
    {
        unsigned t = 0;
        for (ps = 0; ps < f->num_sets; ps++) {
            const double inv = 1.0 / (double)slot_diff[ps];
            unsigned step;
            for (step = 1; step <= slot_diff[ps]; step++, t++) {
                double rg[NB][2], dg[NB][2];
                for (pb = 0; pb < f->param_bands; pb++)
                    for (row = 0; row < 2u; row++) {
                        const double r0 = ps ? resg[ps - 1u][pb][row] : d->resid_prev[pb][row], d0 = ps ? decg[ps - 1u][pb][row] : d->decor_prev[pb][row];
                        rg[pb][row] = r0 + (double)step * inv * (resg[ps][pb][row] - r0);
                        dg[pb][row] = d0 + (double)step * inv * (decg[ps][pb][row] - d0);
                    }
                for (k = 0; k < hyb_count; k++) {
                    const unsigned b = map[k];
                    for (row = 0; row < 2u; row++) {
                        mixed[row][t][k].re = (float)((double)hyb[t][k].re * rg[b][row] + (double)diff[t][k].re * dg[b][row]);
                        mixed[row][t][k].im = (float)((double)hyb[t][k].im * rg[b][row] + (double)diff[t][k].im * dg[b][row]);
                    }
                }
            }
        }
    }
    for (pb = 0; pb < f->param_bands; pb++) for (row = 0; row < 2u; row++) { d->resid_prev[pb][row] = resg[f->num_sets - 1u][pb][row]; d->decor_prev[pb][row] = decg[f->num_sets - 1u][pb][row]; }
    /* 4. hybrid synthesis: the six bands of QMF band 0 and the pairs of QMF bands 1 and 2 are added, the others are QMF bands */
    for (row = 0; row < 2u; row++) {
        SautiAacSbrComplex *o = row ? out1 : out0;
        for (n = 0; n < slots; n++) {
            cxf *q = o + (size_t)n * 64u;
            memset(q, 0, 64u * sizeof(cxf));
            for (k = 0; k < 6u; k++) { q[0].re += mixed[row][n][k].re; q[0].im += mixed[row][n][k].im; }
            q[1].re = mixed[row][n][6].re + mixed[row][n][7].re; q[1].im = mixed[row][n][6].im + mixed[row][n][7].im;
            q[2].re = mixed[row][n][8].re + mixed[row][n][9].re; q[2].im = mixed[row][n][8].im + mixed[row][n][9].im;
            for (k = 3; k < band_count; k++) q[k] = mixed[row][n][7u + k];
        }
    }
    d->ready = 1u;
    return 1;
}
