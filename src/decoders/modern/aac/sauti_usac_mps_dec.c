/* USAC MPEG Surround 212, signal path: see include/sauti_usac_mps_dec.h.  Written for Sauti after studying libxaac's ixheaacd_mps_hybrid_filt.c, ixheaacd_mps_decorr.c, ixheaacd_mps_pre_mix.c and ixheaacd_mps_dec.c
 * (Apache 2.0); the tables come from there (sauti_usac_mps_tables.c).  Arithmetic in double, state in float. */
#include "sauti_usac_mps_dec.h"
#include "sauti_usac_mps_tables.h"
#include <math.h>
#include <stdlib.h>
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
/* hybrid analysis of one channel: QMF band 0 into six, bands 1 and 2 into two hybrid bands each (13 tap filters over the last 12 slots of the previous frame and this frame), the other bands pass */
static void hybrid_in(cxf lf_state[3][12], const SautiAacSbrComplex *in, unsigned band_count, double gain, unsigned slots, cxf (*hyb)[HYB]) {
    cxf lf_in[3][12 + MAXS], split8[6][MAXS], split2[2][MAXS], split2b[2][MAXS];
    unsigned i, k;
    for (k = 0; k < 3u; k++) {
        memcpy(lf_in[k], lf_state[k], 12u * sizeof(cxf));
        for (i = 0; i < slots; i++) { lf_in[k][12u + i].re = (float)(gain * in[(size_t)i * 64u + k].re); lf_in[k][12u + i].im = (float)(gain * in[(size_t)i * 64u + k].im); }
    }
    hyb_split8(lf_in[0], slots, split8);
    hyb_split2(lf_in[1], slots, split2);
    hyb_split2(lf_in[2], slots, split2b);
    for (i = 0; i < slots; i++) {
        for (k = 0; k < 6u; k++) hyb[i][k] = split8[k][i];
        hyb[i][6] = split2[1][i]; hyb[i][7] = split2[0][i];
        hyb[i][8] = split2b[0][i]; hyb[i][9] = split2b[1][i];
        for (k = 3; k < band_count; k++) { hyb[i][7u + k].re = (float)(gain * in[(size_t)i * 64u + k].re); hyb[i][7u + k].im = (float)(gain * in[(size_t)i * 64u + k].im); }
    }
    for (k = 0; k < 3u; k++) memcpy(lf_state[k], lf_in[k] + slots, 12u * sizeof(cxf));
}

void sauti_usac_mps_set_gains(unsigned prediction, int cld_idx, int icc_idx, int ipd_idx, int in_ipd, int in_res, float res[2], float dec[2], float rsd[2], float im[2]) {
    const unsigned c = (unsigned)(cld_idx + 15), ic = (unsigned)icc_idx & 7u;
    im[0] = im[1] = 0.0f;
    if (!prediction) {
        const double cl = sauti_usac_mps_c_l[c], cr = sauti_usac_mps_c_l[30u - c];
        res[0] = (float)((double)sauti_usac_mps_cos[ic][c] * cl);  res[1] = (float)((double)sauti_usac_mps_cos[ic][30u - c] * cr);
        rsd[0] = rsd[1] = 0.0f;
        dec[0] = (float)((double)sauti_usac_mps_sin[ic][c] * cl);  dec[1] = (float)(-(double)sauti_usac_mps_sin[ic][30u - c] * cr);
    } else if (in_ipd && c == 15u && ic == 0u && (ipd_idx & 15) == 8) {      /* identical channels in anti-phase: a fixed gain */
        const float g = 0.416666667f;
        dec[0] = dec[1] = rsd[0] = rsd[1] = 0.0f;
        if (in_res) { res[0] = g; res[1] = g; rsd[0] = g; rsd[1] = -g; } else { res[0] = g; res[1] = -g; }
    } else {
        const unsigned ip = in_ipd ? (unsigned)ipd_idx & 15u : 0u;
        const double w = sauti_usac_mps_weight[ip][ic][c], rw = sauti_usac_mps_re_weight[ip][ic][c], iw = sauti_usac_mps_im_weight[ip][ic][c], beta = sauti_usac_mps_beta[ip][ic][c];
        res[0] = (float)(w - rw); res[1] = (float)(w + rw);
        im[0] = (float)-iw; im[1] = (float)iw;
        dec[0] = (float)beta; dec[1] = (float)-beta;
        rsd[0] = (float)w; rsd[1] = (float)-w;                               /* a band that carries a residual: the signal replaces the decorrelator (the caller zeroes dec there) */
    }
}

/* ---- phase coding (IPD / OPD, stereoConfigIndex 1): the phases are kept in Q28 / Q27 integers exactly as the reference does, because its smoothing is integer arithmetic ---- */
#define PI_Q28 843314880
#define PI_Q27 421657440
static int32_t mult32_shl(int32_t a, int32_t b) { return (int32_t)((uint32_t)(int32_t)(((int64_t)a * (int64_t)b) >> 32) << 1); }
static int32_t wrap_q28(int32_t p) { while (p < 0) p += 2 * PI_Q28; while (p >= 2 * PI_Q28) p -= 2 * PI_Q28; return p; }

/* left / right phase of every parameter band of every set (radians): the overall phase difference of the pair and that minus the inter-channel phase difference, then smoothed over time */
static void phase_sets(SautiUsacMpsDec *d, const SautiUsacMpsConfig *cfg, const SautiUsacMpsFrame *f, const unsigned *slot_diff, float pl[][SAUTI_USAC_MPS_BANDS], float pr[][SAUTI_USAC_MPS_BANDS]) {
    unsigned ps, pb;
    const unsigned nb_ipd = f->phase_mode ? cfg->bands_phase : 0u;
    for (ps = 0; ps < f->num_sets; ps++)
        for (pb = 0; pb < f->param_bands; pb++) {
            if (pb < nb_ipd) {
                const unsigned c = (unsigned)(f->cld_idx[ps][pb] + 15), ipd = (unsigned)f->ipd_idx[ps][pb] & 15u, ic = (unsigned)f->icc_idx[ps][pb] & 7u;
                const int32_t opd = (c == 15u && ipd == 8u) ? 0 : sauti_usac_mps_atan_q28[ipd][ic][c];
                pl[ps][pb] = (float)wrap_q28(opd) * (1.0f / 268435456.0f);
                pr[ps][pb] = (float)wrap_q28(opd - sauti_usac_mps_ipd_dequant_q28[ipd]) * (1.0f / 268435456.0f);
            } else pl[ps][pb] = pr[ps][pb] = 0.0f;
        }
    if (!f->opd_smoothing) {
        for (pb = 0; pb < f->param_bands; pb++) { d->smooth_phase[0][pb] = (int32_t)(pl[f->num_sets - 1u][pb] * 268435456.0f) >> 1; d->smooth_phase[1][pb] = (int32_t)(pr[f->num_sets - 1u][pb] * 268435456.0f) >> 1; }
        return;
    }
    for (ps = 0; ps < f->num_sets; ps++) {
        const int32_t thr = f->ipd_coarse[ps] ? 117127067 : 58563533;
        const int32_t delta = (int32_t)(slot_diff[ps] * 8388608u), one_minus = 1073741824 - delta;
        for (pb = 0; pb < f->param_bands; pb++) {
            int32_t lt = (int32_t)(pl[ps][pb] * 268435456.0f) >> 1, rt = (int32_t)(pr[ps][pb] * 268435456.0f) >> 1, tmp;
            int32_t *sl = &d->smooth_phase[0][pb], *sr = &d->smooth_phase[1][pb];
            while (lt > *sl + PI_Q27) lt -= 2 * PI_Q27;
            while (lt < *sl - PI_Q27) lt += 2 * PI_Q27;
            while (rt > *sr + PI_Q27) rt -= 2 * PI_Q27;
            while (rt < *sr - PI_Q27) rt += 2 * PI_Q27;
            *sl = (int32_t)((uint32_t)(mult32_shl(delta, lt) + mult32_shl(one_minus, *sl)) << 1);
            *sr = (int32_t)((uint32_t)(mult32_shl(delta, rt) + mult32_shl(one_minus, *sr)) << 1);
            tmp = (lt - rt) - (*sl - *sr);
            while (tmp > PI_Q27) tmp -= 2 * PI_Q27;
            while (tmp < -PI_Q27) tmp += 2 * PI_Q27;
            if (tmp > thr || tmp < -thr) { *sl = lt; *sr = rt; }
            while (*sl > 2 * PI_Q27) *sl -= 2 * PI_Q27;
            while (*sl < 0) *sl += 2 * PI_Q27;
            while (*sr > 2 * PI_Q27) *sr -= 2 * PI_Q27;
            while (*sr < 0) *sr += 2 * PI_Q27;
            pl[ps][pb] = (float)(int32_t)((uint32_t)*sl << 1) * (1.0f / 268435456.0f);
            pr[ps][pb] = (float)(int32_t)((uint32_t)*sr << 1) * (1.0f / 268435456.0f);
        }
    }
}

/* interpolation of an angle between two sets over the short way round; returns the value one step in and the step (both as the reference's float arithmetic does) */
static float angle_interp(float a1, float a2, float alpha, float *step) {
    const float p = (float)3.14159265358979;
    while (a2 - a1 > p) a1 += 2.0f * p;
    while (a1 - a2 > p) a2 += 2.0f * p;
    *step = a2 - a1;
    return (1.0f - alpha) * a1 + alpha * a2;
}

/* ---- STP, subband temporal processing (bsTempShapeConfig 1): the decorrelated part of the output is scaled slot by slot so that its temporal envelope in the hybrid bands 13..31 follows that of the direct part.  The energies are
 * kept in the units of the reference (our QMF grids are 1 / (32768 * sqrt 2) of its) because the start values and the thresholds are absolute ---- */
#define STP_BP 25
static const float stp_bp[STP_BP] = { 0.0000f, 0.0005f, 0.0092f, 0.0587f, 0.2580f, 0.7392f, 0.9791f, 0.9993f, 1.0000f, 1.0000f, 1.0000f, 1.0000f, 0.9999f, 0.9984f, 0.9908f, 0.9639f, 0.8952f, 0.7711f, 0.6127f, 0.4609f, 0.3391f, 0.2493f, 0.1848f, 0.1387f, 0.1053f };
static const float stp_gf[STP_BP] = { 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1e-008f, 8.1e-007f, 3.61e-006f, 8.41e-006f, 1.6e-005f, 2.704e-005f, 3.969e-005f, 5.625e-005f, 7.396e-005f, 9.801e-005f, 0.00012321f, 0.00015625f, 0.00019881f, 0.00024964f, 0.00032041f, 0.00041209f, 0.00053824f, 0.00070756f, 0.00094249f };
static const unsigned stp_h2q[10] = { 0, 0, 0, 0, 0, 0, 1, 1, 2, 2 };

static void stp_scale(SautiUsacMpsDec *d, cxf (*dir)[MAXS][HYB], cxf (*dif)[MAXS][HYB], unsigned ts, float scale[2]) {
    const float K = 2147483648.0f, abs_thr = 1.073741824f;                        /* 2 * 32768^2: our grids are 1 / (32768 sqrt 2) of the reference's; 1e-9 * 32768^2 */
    float dir_e = 0.0f, diff_e[2];
    unsigned n, ch;
    if (!d->stp_init) { for (ch = 0; ch < 2u; ch++) { d->stp_scale_last[ch] = 1.0f; d->stp_nrg_diff_prev[ch] = 32768.0f * 32768.0f; } d->stp_nrg_dir_prev = 32768.0f * 32768.0f; d->stp_init = 1u; }
    if (d->stp_update == 32u) { d->stp_update = 1u; d->stp_nrg_dir_prev = d->stp_nrg_dir; for (ch = 0; ch < 2u; ch++) d->stp_nrg_diff_prev[ch] = d->stp_nrg_diff[ch]; } else d->stp_update++;
    for (n = 6; n < STP_BP; n++) {
        const float lr = dir[0][ts][n + 7].re, rr = dir[1][ts][n + 7].re, li = dir[0][ts][n + 7].im, ri = dir[1][ts][n + 7].im;
        const float t = ((lr + rr) * (lr + rr) + (li + ri) * (li + ri)) * K;
        dir_e += t * stp_bp[n] * stp_bp[n] * stp_gf[n] * stp_gf[n];
    }
    d->stp_nrg_dir = (float)(0.950f * d->stp_nrg_dir + (1.0 - 0.950f) * dir_e);
    dir_e /= (d->stp_nrg_dir_prev + abs_thr);
    for (ch = 0; ch < 2u; ch++) {
        diff_e[ch] = 0.0f;
        for (n = 6; n < STP_BP; n++) {
            const float t = (dif[ch][ts][n + 7].re * dif[ch][ts][n + 7].re + dif[ch][ts][n + 7].im * dif[ch][ts][n + 7].im) * K;
            diff_e[ch] += t * stp_bp[n] * stp_bp[n] * stp_gf[n] * stp_gf[n];
        }
        d->stp_nrg_diff[ch] = (float)(0.950f * d->stp_nrg_diff[ch] + (1.0 - 0.950f) * diff_e[ch]);
        diff_e[ch] /= (d->stp_nrg_diff_prev[ch] + abs_thr);
    }
    for (ch = 0; ch < 2u; ch++) {
        float v = (float)sqrt(dir_e / (diff_e[ch] + 1e-9f));
        v = 0.1f + (1.0f - 0.1f) * v;
        if (v < (float)(1.0 / 2.82f)) v = (float)(1.0 / 2.82f);
        if (v > 2.82f) v = 2.82f;
        v = (float)(0.450f * v + (1.0 - 0.450f) * d->stp_scale_last[ch]);
        d->stp_scale_last[ch] = v; scale[ch] = v;
    }
}

/* dir / dif: the direct and the decorrelated hybrid outputs [channel][slot][band]; the result (direct + scaled decorrelated) replaces dir */
static void stp_apply(SautiUsacMpsDec *d, const SautiUsacMpsFrame *f, unsigned slots, unsigned hyb_count, unsigned dif_s, cxf (*dir)[MAXS][HYB], cxf (*dif)[MAXS][HYB]) {
    unsigned ch, ts, n;
    for (ch = 0; ch < 2u; ch++) for (ts = 0; ts < slots; ts++) for (n = dif_s; n < 12u && n < hyb_count; n++) { dir[ch][ts][n].re += dif[ch][ts][n].re; dir[ch][ts][n].im += dif[ch][ts][n].im; dif[ch][ts][n].re = 0.0f; dif[ch][ts][n].im = 0.0f; }
    for (ts = 0; ts < slots; ts++) {
        float scale[2];
        stp_scale(d, dir, dif, ts, scale);
        for (ch = 0; ch < 2u; ch++) {
            if (!f->stp_ch[ch]) { for (n = 0; n < hyb_count; n++) { dir[ch][ts][n].re += dif[ch][ts][n].re; dir[ch][ts][n].im += dif[ch][ts][n].im; } continue; }
            for (n = 0; n < 10u && n < hyb_count; n++) { const float t = scale[ch] * stp_bp[stp_h2q[n]]; dir[ch][ts][n].re += dif[ch][ts][n].re * t; dir[ch][ts][n].im += dif[ch][ts][n].im * t; }
            for (n = 7; n < 16u && n < hyb_count; n++) { const float t = scale[ch] * stp_bp[n - 7u]; dir[ch][ts][n].re += dif[ch][ts][n].re * t; dir[ch][ts][n].im += dif[ch][ts][n].im * t; }
            for (; n < hyb_count; n++) { dir[ch][ts][n].re += dif[ch][ts][n].re * scale[ch]; dir[ch][ts][n].im += dif[ch][ts][n].im * scale[ch]; }
        }
    }
}

/* ---- GES, guided envelope shaping (bsTempShapeConfig 2): the temporal envelope of the direct part of each output channel (hybrid bands from 6) is scaled so that it follows the transmitted envelope, relative to the envelope of the
 * downmix.  Both are estimated on the parameter bands 10..18 of the 20 band map with the energies whitened by their smoothed band energies; units of the reference again (our grids are 1 / (32768 sqrt 2) of its) ---- */
#define GES_ALPHA 0.99637864f
#define GES_BETA 0.9643691f
#define GES_LAMBDA 4.0f

static void ges_norm_env(SautiUsacMpsDec *d, unsigned idx, const float (*slot_e)[20], unsigned slots, float *env) {
    const float abs_thr = 1.073741824f;
    float pb[20] = { 0 }, w[20], avg, total;
    unsigned t, p;
    if (!d->ges_init) { for (t = 0; t < 3u; t++) d->ges_avg[t] = 32768.0f * 32768.0f; d->ges_init = 1u; }
    for (p = 10; p <= 18u; p++) pb[p] = d->ges_pb[idx][p];
    avg = d->ges_avg[idx];
    for (t = 0; t < slots; t++) {
        float e = 0.0f;
        total = 0.0f;
        for (p = 10; p <= 18u; p++) { pb[p] = (1.0f - GES_ALPHA) * slot_e[t][p] + GES_ALPHA * pb[p]; total += slot_e[t][p]; }
        total /= 9.0f;
        total = (1.0f - GES_ALPHA) * total + GES_ALPHA * d->ges_frame[idx];
        d->ges_frame[idx] = total;
        for (p = 10; p <= 18u; p++) w[p] = total / (pb[p] + abs_thr);
        for (p = 10; p <= 18u; p++) e += slot_e[t][p] * w[p];
        avg = (1.0f - GES_BETA) * e + GES_BETA * avg;
        env[t] = (float)sqrt(e / (avg + abs_thr));
    }
    for (p = 10; p <= 18u; p++) d->ges_pb[idx][p] = pb[p];
    d->ges_avg[idx] = avg;
}

/* dir / dif: [channel][slot][band] direct and decorrelated parts (the result of the shaping is dir + dif in dir); hyb: the hybrid downmix [slot][band] */
static void ges_apply(SautiUsacMpsDec *d, const SautiUsacMpsFrame *f, unsigned slots, unsigned hyb_count, const cxf (*hyb)[HYB], cxf (*dir)[MAXS][HYB], cxf (*dif)[MAXS][HYB]) {
    const float K = 2147483648.0f, abs_thr = 1.073741824f;
    float slot_e[MAXS][20], dmx[MAXS], env[MAXS];
    unsigned ch, t, k;
    memset(slot_e, 0, sizeof slot_e);
    for (t = 0; t < slots; t++) for (k = 0; k < hyb_count; k++) slot_e[t][sauti_usac_mps_band_map_20[k]] += (hyb[t][k].re * hyb[t][k].re + hyb[t][k].im * hyb[t][k].im) * K;
    ges_norm_env(d, 2u, (const float (*)[20])slot_e, slots, dmx);
    for (ch = 0; ch < 2u; ch++) {
        memset(slot_e, 0, sizeof slot_e);
        for (t = 0; t < slots; t++) for (k = 0; k < hyb_count; k++) {
            const float re = dir[ch][t][k].re + dif[ch][t][k].re, im = dir[ch][t][k].im + dif[ch][t][k].im;
            slot_e[t][sauti_usac_mps_band_map_20[k]] += (re * re + im * im) * K;
        }
        ges_norm_env(d, ch, (const float (*)[20])slot_e, slots, env);
        if (!f->ges_ch[ch]) continue;
        for (t = 0; t < slots; t++) {
            const float gain = f->ges_env[ch][t] * dmx[t] / (env[t] + 1e-9f);
            float ad = 0.0f, af = 0.0f, ratio;
            for (k = 6; k < hyb_count; k++) {
                ad += dir[ch][t][k].re * dir[ch][t][k].re + dir[ch][t][k].im * dir[ch][t][k].im;
                af += dif[ch][t][k].re * dif[ch][t][k].re + dif[ch][t][k].im * dif[ch][t][k].im;
            }
            ratio = (float)sqrt((af * K) / (ad * K + abs_thr));
            ratio = gain + ratio * (gain - 1.0f);
            if (ratio < 1.0f / GES_LAMBDA) ratio = 1.0f / GES_LAMBDA;
            if (ratio > GES_LAMBDA) ratio = GES_LAMBDA;
            for (k = 6; k < hyb_count; k++) { dir[ch][t][k].re *= ratio; dir[ch][t][k].im *= ratio; }
        }
    }
}

void sauti_usac_mps212_residual_grid(SautiUsacMpsDec *d, const SautiAacSbrComplex *low, unsigned slots, unsigned lag_lo, unsigned lag_hi, SautiAacSbrComplex *grid) {
    unsigned i, k;
    if (lag_lo > 12u) lag_lo = 12u;
    if (lag_hi > 12u) lag_hi = 12u;
    if (slots < 12u) slots = 12u;
    memset(grid, 0, (size_t)slots * 64u * sizeof *grid);
    for (i = 0; i < slots; i++)
        for (k = 0; k < 32u; k++) {
            const unsigned lag = k < 3u ? lag_lo : lag_hi;
            grid[(size_t)i * 64u + k] = i < lag ? d->res_tail[12u - lag + i][k] : low[(size_t)(i - lag) * 32u + k];
        }
    for (i = 0; i < 12u; i++) memcpy(d->res_tail[i], low + (size_t)(slots - 12u + i) * 32u, 32u * sizeof *low);
}

void sauti_usac_mps212_downmix_grid(SautiUsacMpsDec *d, const SautiAacSbrComplex *low, unsigned slots, unsigned lag_lo, unsigned lag_hi, SautiAacSbrComplex *grid) {
    unsigned i, k;
    if (lag_lo > 12u) lag_lo = 12u;
    if (lag_hi > 12u) lag_hi = 12u;
    if (slots < 12u) slots = 12u;
    memset(grid, 0, (size_t)slots * 64u * sizeof *grid);
    for (i = 0; i < slots; i++)
        for (k = 0; k < 32u; k++) {
            const unsigned lag = k < 3u ? lag_lo : lag_hi;
            grid[(size_t)i * 64u + k] = i < lag ? d->dmx_tail[12u - lag + i][k] : low[(size_t)(i - lag) * 32u + k];
        }
    for (i = 0; i < 12u; i++) memcpy(d->dmx_tail[i], low + (size_t)(slots - 12u + i) * 32u, 32u * sizeof *low);
}

int sauti_usac_mps212_apply(SautiUsacMpsDec *d, const SautiUsacMpsConfig *cfg, const SautiUsacMpsFrame *f, unsigned band_count, unsigned slots,
                            const SautiAacSbrComplex *in, const SautiAacSbrComplex *res_in, unsigned res_band_count, SautiAacSbrComplex *out0, SautiAacSbrComplex *out1) {
    cxf hyb[MAXS][HYB], diff[MAXS][HYB], mixed[2][MAXS][HYB], hres[MAXS][HYB];                 /* about 160 KiB of scratch on the stack */
    float resg[SAUTI_USAC_MPS_SETS][NB][2], decg[SAUTI_USAC_MPS_SETS][NB][2], rsdg[SAUTI_USAC_MPS_SETS][NB][2], img[SAUTI_USAC_MPS_SETS][NB][2];
    unsigned slot_diff[SAUTI_USAC_MPS_SETS];
    cxf (*mixed_d)[MAXS][HYB] = NULL;                                              /* STP: the decorrelated part of the output on its own */
    float pl[SAUTI_USAC_MPS_SETS][NB], pr[SAUTI_USAC_MPS_SETS][NB];
    float phc[MAXS][NB][2], phs[MAXS][NB][2];                           /* cosine / sine of the interpolated phase per slot, parameter band and output channel */
    const uint8_t *map, *map28 = sauti_usac_mps_band_map_28;
    const unsigned *split;
    unsigned hyb_count, res_hyb_count = 0, i, k, pb, n, ps, row;
    const double gain = cfg ? sauti_usac_mps_clip_gain[cfg->fixed_gain & 7u] : 1.0;
    if (!d || !cfg || !f || !in || !out0 || !out1 || slots == 0 || slots > MAXS || band_count < 4u || band_count > 64u || cfg->fixed_gain > 7u || cfg->decorr > 2u) return 0;
    if ((cfg->phase_coding && cfg->bands_phase > f->param_bands) || cfg->residual_bands > f->param_bands || (cfg->residual_bands && (!res_in || res_band_count < 4u || res_band_count > 64u)) || f->num_sets == 0 || f->num_sets > SAUTI_USAC_MPS_SETS || cfg->freq_res == 0 || cfg->freq_res > 7u ) return 0;
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
    /* 1. hybrid analysis of the (gain scaled) downmix and, in the bands that carry one, of the residual */
    memset(hyb, 0, sizeof hyb);
    hybrid_in(d->lf, in, band_count, gain, slots, hyb);
    if (cfg->residual_bands) {
        res_hyb_count = res_band_count - 3u + 10u;
        memset(hres, 0, sizeof hres);
        hybrid_in(d->lf_res, res_in, res_band_count, gain, slots, hres);
    }
    /* 2. decorrelator: per hybrid band a delay and an all-pass filter, then the energy of every parameter band (of the 28 band map) is brought back towards that of the input */
    memset(diff, 0, sizeof diff);
    for (k = 0; k < hyb_count; k++) {
        unsigned rb = 0, delay, len;
        cxf buf[SAUTI_USAC_MPS_DELAY_MAX + MAXS];
        while (rb < 3u && (int)sauti_usac_mps_hybrid_to_qmf[k] >= (int)split[rb] - 1) rb++;               /* signed: decorrelator configuration 2 starts with a split frequency of 0 (-1 passes every band) */
        delay = decor_delay[rb]; len = lat_len[rb];
        for (n = 0; n < delay; n++) buf[n] = d->delay[k][n];
        for (n = 0; n < slots; n++) { const int tsd = cfg->temp_shape == 3u && f->tsd_enable && f->tsd_sep[n] && k >= 7u; buf[delay + n] = hyb[n][k]; if (tsd) { buf[delay + n].re = 0.0f; buf[delay + n].im = 0.0f; } }   /* TSD: a transient slot does not feed the decorrelator above hybrid band 6 */
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
            if (!(cfg->temp_shape == 3u && f->tsd_enable && f->tsd_sep[n] && k >= 7u)) in_e[map28[k]] += (double)hyb[n][k].re * hyb[n][k].re + (double)hyb[n][k].im * hyb[n][k].im;
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
        if (cfg->temp_shape == 3u && f->tsd_enable && f->tsd_sep[n]) {                 /* TSD: the transient slot carries the downmix itself, rotated by 45 degrees times the transmitted phase, in place of its decorrelation */
            static const float mre[8] = { 1.0f, 0.707106781186548f, 0.0f, -0.707106781186548f, -1.0f, -0.707106781186548f, 0.0f, 0.707106781186548f };
            static const float mim[8] = { 0.0f, 0.707106781186548f, 1.0f, 0.707106781186548f, 0.0f, -0.707106781186548f, -1.0f, -0.707106781186548f };
            const float cr = mre[f->tsd_phase[n] & 7u], ci = mim[f->tsd_phase[n] & 7u];
            for (k = 7; k < hyb_count; k++) { diff[n][k].re += cr * hyb[n][k].re - ci * hyb[n][k].im; diff[n][k].im += ci * hyb[n][k].re + cr * hyb[n][k].im; }
        }
    }
    /* 3. upmix gains of every parameter set, interpolated slot by slot, and the mix of the direct and the decorrelated signal */
    for (ps = 0; ps < f->num_sets; ps++)
        for (pb = 0; pb < f->param_bands; pb++) {
            float rs[2], dc[2], rd[2], ii[2];
            const int in_res = pb < cfg->residual_bands;                        /* a band below bsResidualBands: the residual signal takes the place of the decorrelator output */
            const int in_ipd = cfg->phase_coding && f->phase_mode && pb < cfg->bands_phase;
            sauti_usac_mps_set_gains(cfg->residual, f->cld_idx[ps][pb], f->icc_idx[ps][pb], in_ipd ? f->ipd_idx[ps][pb] : 0, in_ipd, in_res, rs, dc, rd, ii);
            for (row = 0; row < 2u; row++) {
                resg[ps][pb][row] = rs[row]; decg[ps][pb][row] = in_res ? 0.0f : dc[row]; rsdg[ps][pb][row] = in_res ? rd[row] : 0.0f; img[ps][pb][row] = ii[row];
            }
        }
    /* smoothing of the upmix gains over time (bsSmoothMode 1..3, only with bsHighRateMode): g[ps] = delta g[ps] + (1 - delta) g[ps - 1] with delta = distance of the sets / smoothing time, in the bands above the residual
     * ones that are flagged; the first set is smoothed towards the last (already smoothed) set of the previous frame */
    for (ps = 0; ps < f->num_sets; ps++) {
        const float delta = (float)slot_diff[ps] * (1.0f / (float)(f->smooth_time[ps] ? f->smooth_time[ps] : 256u)), one_minus = 1.0f - delta;
        for (pb = cfg->residual_bands; pb < f->param_bands; pb++) {
            if (!f->smooth_data[ps][pb]) continue;
            for (row = 0; row < 2u; row++) {
                const float pr = ps ? resg[ps - 1u][pb][row] : d->resid_prev[pb][row], pd = ps ? decg[ps - 1u][pb][row] : d->decor_prev[pb][row], ps_ = ps ? rsdg[ps - 1u][pb][row] : d->rsd_prev[pb][row], pi = ps ? img[ps - 1u][pb][row] : d->im_prev[pb][row];
                resg[ps][pb][row] = delta * resg[ps][pb][row] + one_minus * pr;
                decg[ps][pb][row] = delta * decg[ps][pb][row] + one_minus * pd;
                rsdg[ps][pb][row] = delta * rsdg[ps][pb][row] + one_minus * ps_;
                img[ps][pb][row] = delta * img[ps][pb][row] + one_minus * pi;
            }
        }
    }
    if (cfg->phase_coding && !cfg->residual) {
        unsigned t = 0;
        phase_sets(d, cfg, f, slot_diff, pl, pr);
        for (ps = 0; ps < f->num_sets; ps++) {
            const float alpha = (float)(1.0 / (double)slot_diff[ps]);
            for (pb = 0; pb < f->param_bands; pb++) {
                float sl, sr, tl = angle_interp(ps ? pl[ps - 1u][pb] : d->phase_prev[0][pb], pl[ps][pb], alpha, &sl), tr = angle_interp(ps ? pr[ps - 1u][pb] : d->phase_prev[1][pb], pr[ps][pb], alpha, &sr);
                unsigned i2;
                sl *= alpha; sr *= alpha;
                for (i2 = 0; i2 < slot_diff[ps]; i2++) {
                    phc[t + i2][pb][0] = (float)cos((double)tl); phs[t + i2][pb][0] = (float)sin((double)tl); tl += sl;
                    phc[t + i2][pb][1] = (float)cos((double)tr); phs[t + i2][pb][1] = (float)sin((double)tr); tr += sr;
                }
            }
            t += slot_diff[ps];
        }
        for (pb = 0; pb < f->param_bands; pb++) { d->phase_prev[0][pb] = pl[f->num_sets - 1u][pb]; d->phase_prev[1][pb] = pr[f->num_sets - 1u][pb]; }
    }
    memset(mixed, 0, sizeof mixed);
    if (cfg->temp_shape == 1u || cfg->temp_shape == 2u) {
        mixed_d = (cxf (*)[MAXS][HYB])calloc(2u * MAXS * HYB, sizeof(cxf));
        if (!mixed_d) return 0;
    }
    {
        unsigned t = 0;
        for (ps = 0; ps < f->num_sets; ps++) {
            const double inv = 1.0 / (double)slot_diff[ps];
            unsigned step;
            for (step = 1; step <= slot_diff[ps]; step++, t++) {
                double rg[NB][2], dg[NB][2], sg[NB][2], ig[NB][2];
                for (pb = 0; pb < f->param_bands; pb++)
                    for (row = 0; row < 2u; row++) {
                        const double r0 = ps ? resg[ps - 1u][pb][row] : d->resid_prev[pb][row], d0 = ps ? decg[ps - 1u][pb][row] : d->decor_prev[pb][row], s0 = ps ? rsdg[ps - 1u][pb][row] : d->rsd_prev[pb][row];
                        rg[pb][row] = r0 + (double)step * inv * (resg[ps][pb][row] - r0);
                        dg[pb][row] = d0 + (double)step * inv * (decg[ps][pb][row] - d0);
                        sg[pb][row] = s0 + (double)step * inv * (rsdg[ps][pb][row] - s0);
                        { const double i0 = ps ? img[ps - 1u][pb][row] : d->im_prev[pb][row]; ig[pb][row] = i0 + (double)step * inv * (img[ps][pb][row] - i0); }
                    }
                for (k = 0; k < hyb_count; k++) {
                    const unsigned b = map[k];
                    if (cfg->phase_coding && !cfg->residual) {                  /* complex mix: each signal is rotated by its output channel's phase; the two lowest hybrid bands (negative frequencies of the split) the other way */
                        for (row = 0; row < 2u; row++) {
                            const float c = phc[t][b][row], sn = phs[t][b][row], g1 = (float)rg[b][row], g2 = (float)dg[b][row], gc1 = g1 * c, gs1 = g1 * sn, gc2 = g2 * c, gs2 = g2 * sn;
                            const float sg_ = k < 2u ? 1.0f : -1.0f;
                            mixed[row][t][k].re = hyb[t][k].re * gc1 + sg_ * hyb[t][k].im * gs1 + diff[t][k].re * gc2 + sg_ * diff[t][k].im * gs2;
                            mixed[row][t][k].im = hyb[t][k].im * gc1 - sg_ * hyb[t][k].re * gs1 + diff[t][k].im * gc2 - sg_ * diff[t][k].re * gs2;
                        }
                        continue;
                    }
                    for (row = 0; row < 2u; row++) {
                        const int has_res = cfg->residual_bands && k < res_hyb_count;
                        mixed[row][t][k].re = (float)((double)hyb[t][k].re * rg[b][row] + (double)diff[t][k].re * dg[b][row] + (has_res ? (double)hres[t][k].re * sg[b][row] : 0.0));
                        mixed[row][t][k].im = (float)((double)hyb[t][k].im * rg[b][row] + (double)diff[t][k].im * dg[b][row] + (has_res ? (double)hres[t][k].im * sg[b][row] : 0.0));
                        if (cfg->phase_coding) {                                  /* the imaginary part of the prediction matrix acts on the direct signal only; opposite sign in the two lowest hybrid bands */
                            const double gi = k < 2u ? ig[b][row] : -ig[b][row];
                            mixed[row][t][k].re = (float)((double)mixed[row][t][k].re + (double)hyb[t][k].im * gi);
                            mixed[row][t][k].im = (float)((double)mixed[row][t][k].im - (double)hyb[t][k].re * gi);
                        }
                    }
                }
                if (mixed_d) for (k = 0; k < hyb_count; k++) {
                    const unsigned b = map[k];
                    for (row = 0; row < 2u; row++) {
                        if (cfg->phase_coding && !cfg->residual) {
                            const float c = phc[t][b][row], sn = phs[t][b][row], g2 = (float)dg[b][row], gc2 = g2 * c, gs2 = g2 * sn, sg_ = k < 2u ? 1.0f : -1.0f;
                            mixed_d[row][t][k].re = diff[t][k].re * gc2 + sg_ * diff[t][k].im * gs2;
                            mixed_d[row][t][k].im = diff[t][k].im * gc2 - sg_ * diff[t][k].re * gs2;
                        } else { mixed_d[row][t][k].re = (float)((double)diff[t][k].re * dg[b][row]); mixed_d[row][t][k].im = (float)((double)diff[t][k].im * dg[b][row]); }
                    }
                }
            }
        }
    }
    if (mixed_d) {                                                              /* STP: split the output into its direct and decorrelated parts, then let the shaping recombine them */
        for (row = 0; row < 2u; row++) for (n = 0; n < slots; n++) for (k = 0; k < hyb_count; k++) { mixed[row][n][k].re -= mixed_d[row][n][k].re; mixed[row][n][k].im -= mixed_d[row][n][k].im; }
        if (cfg->temp_shape == 2u) {                                            /* GES shapes the direct part, then the decorrelated part is added unchanged */
            ges_apply(d, f, slots, hyb_count > res_hyb_count ? hyb_count : res_hyb_count, (const cxf (*)[HYB])hyb, mixed, mixed_d);
            for (row = 0; row < 2u; row++) for (n = 0; n < slots; n++) for (k = 0; k < hyb_count; k++) { mixed[row][n][k].re += mixed_d[row][n][k].re; mixed[row][n][k].im += mixed_d[row][n][k].im; }
        } else if (cfg->residual_bands != 28u) stp_apply(d, f, slots, hyb_count, cfg->residual_bands ? (unsigned)sauti_usac_mps_gain_set_index[cfg->residual_bands] : 0u, mixed, mixed_d);
        else for (row = 0; row < 2u; row++) for (n = 0; n < slots; n++) for (k = 0; k < hyb_count; k++) { mixed[row][n][k].re += mixed_d[row][n][k].re; mixed[row][n][k].im += mixed_d[row][n][k].im; }
        free(mixed_d); mixed_d = NULL;
    }
    for (pb = 0; pb < f->param_bands; pb++) for (row = 0; row < 2u; row++) { d->resid_prev[pb][row] = resg[f->num_sets - 1u][pb][row]; d->decor_prev[pb][row] = decg[f->num_sets - 1u][pb][row]; d->rsd_prev[pb][row] = rsdg[f->num_sets - 1u][pb][row]; d->im_prev[pb][row] = img[f->num_sets - 1u][pb][row]; }
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
