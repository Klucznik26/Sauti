/* USAC LPD frame decoder (stages L2 - L4 of docs/USAC_LPD_PLAN.md): ACELP (adaptive + algebraic codebook, gains, post-processing, synthesis), TCX (noise filling, LPC based spectral shaping, windowed IMDCT with
 * low overlap, gain), forward aliasing cancellation (FAC) and the long-term (bass) post-filter, ISO/IEC 23003-3 clause 7.13 - 7.15.  Float arithmetic, PCM scale (+-32768) all through.  The structure of the
 * decoder (buffer layout, order of the stages) follows the reference decoder of the standard as implemented by libxaac (Apache License 2.0, see third_party/libxaac/): the code is written for Sauti, the numbers
 * (tables) are generated from libxaac's source by tools/gen_usac_lpd_tables.py.  The result is verified against libxaac's decoder (scripts/test_usac_lpd_vs_xaac.sh), stage by stage with an instrumented copy. */
#include "sauti_usac_lpd_dec.h"
#include "sauti_usac_lpd_tables.h"
#include <math.h>
#include <string.h>

#define ORDER 16u
#define LEN_SUBFR 64u
#define MAX_PITCH SAUTI_LPD_MAX_PITCH
#define EXC_PAD (MAX_PITCH + 17u)                        /* MAX_PITCH + INTER_LP_FIL_ORDER + 1 */
#define FILTER_DELAY 12
#define PREEMPH 0.68f
#define TILT_CODE 0.3f
#define PI_D 3.14159265358979323846

/* ---- small filters ---- */
static void preemph(float *x, float mu, int len, float mem) {
    int i;
    for (i = len - 1; i > 0; i--) x[i] -= mu * x[i - 1];
    x[0] -= mu * mem;
}
static void deemph(float *x, int len, float mem) {
    int i;
    x[0] += PREEMPH * mem;
    for (i = 1; i < len; i++) x[i] += PREEMPH * x[i - 1];
}
/* y = x + sum a[j] x[i-j] over `blocks` blocks of l samples, a advances by 17 per block; x[-16..-1] is the history */
static void residual(const float *a, const float *x, float *y, int l, int blocks) {
    int i, j, b;
    for (b = 0; b < blocks; b++) {
        for (i = 0; i < l; i++) {
            float s = x[i];
            for (j = 1; j <= 16; j++) s += a[j] * x[i - j];
            y[i] = s;
        }
        a += 17; x += l; y += l;
    }
}
/* synthesis filter 1/A(z) in place, x[-16..-1] is the history */
static void synth_inplace(const float *a, float *x, int l) {
    int i, j;
    for (i = 0; i < l; i++) {
        float s = x[i];
        for (j = 1; j <= 16; j++) s -= a[j] * x[i - j];
        x[i] = s;
    }
}
/* y = 1/A(z) x with memory mem[16] (the last 16 outputs) */
static void synth_mem(const float *a, const float *x, float *y, int l, const float *mem) {
    float buf[16 + 512];
    float *yy = buf + 16;
    int i, j;
    memcpy(buf, mem, 16 * sizeof(float));
    for (i = 0; i < l; i++) {
        float s = x[i];
        for (j = 1; j <= 16; j++) s -= a[j] * yy[i - j];
        yy[i] = s;
        y[i] = s;
    }
}
static void weight_filter(const float *a, float *ap, float gamma) {
    float f = gamma;
    unsigned i;
    ap[0] = a[0];
    for (i = 1; i <= 16u; i++) { ap[i] = f * a[i]; f *= gamma; }
}
/* synthesis with the weighted filter A(z / 0.92) in place (FAC) */
static void synth_weighted_inplace(const float *a, float *x, int l) {
    float ap[17];
    weight_filter(a, ap, 0.92f);
    synth_inplace(ap, x, l);
}

/* ---- DCT-IV and the low-overlap IMDCT built on it ---- */
/* out[k] = sum_n in[n] cos(pi / N (n + 1/2)(k + 1/2)) */
static void dct4(const float *in, float *out, int n) {
    int k, i;
    for (k = 0; k < n; k++) {
        const double th = PI_D * (k + 0.5) / n, c1 = cos(th), s1 = sin(th);
        double cr = cos(0.5 * th), sr = sin(0.5 * th), acc = 0.0;          /* angle (i + 1/2) * th, rotated by th per sample */
        for (i = 0; i < n; i++) {
            acc += (double)in[i] * cr;
            { const double t = cr * c1 - sr * s1; sr = sr * c1 + cr * s1; cr = t; }
        }
        out[k] = (float)acc;
    }
}
/* the unfolding of the standard's IMDCT with overlap l (both sides) and m flat lines: out has 2l + m samples */
static void imdct_main(const float *in, float *out, int l, int m) {
    float ob[1280];
    int i;
    dct4(in, ob, l + m);
    for (i = 0; i < m / 2; i++) out[l + m / 2 - 1 - i] = -ob[m / 2 + l / 2 + i];
    for (i = 0; i < l / 2; i++) { out[i] = ob[m + l / 2 + i]; out[l - 1 - i] = -ob[m + l / 2 + i]; }
    for (i = 0; i < m / 2; i++) out[l + m / 2 + i] = -ob[m / 2 + l / 2 - 1 - i];
    for (i = 0; i < l / 2; i++) { out[l + m + i] = -ob[l / 2 - 1 - i]; out[2 * l + m - 1 - i] = -ob[l / 2 - 1 - i]; }
}
static float sine_win(unsigned n, unsigned i) { return (float)sin(PI_D * ((double)i + 0.5) / (2.0 * n)); }   /* rising half of a sine window of 2 * n... n samples long: n = window length */

/* ---- FAC: the transform coded part ---- */
static int fac_values(const SautiUsacLpdFac *fac, int32_t *out) {
    unsigned i;
    for (i = 0; i < fac->count; i++) {
        static const uint16_t zero[8] = { 0 };
        const SautiUsacLpdAvq *v = &fac->blk[i];
        if (!sauti_usac_lpd_avq_decode(v->qn, v->index, v->qn ? v->kv : zero, out + 8u * i)) return 0;
    }
    return 1;
}

/* ---- ACELP ---- */
static void add_one(float *c, unsigned pos, unsigned track, int neg) { const unsigned m = (pos << 2) + track; c[m] += neg ? -1.0f : 1.0f; }
static void pulses_1(unsigned idx, unsigned M, unsigned off, unsigned track, float *c) { add_one(c, (idx & ((1u << M) - 1u)) + off, track, (idx >> M) & 1u); }
static void pulses_2(unsigned idx, unsigned M, unsigned off, unsigned track, float *c) {
    const unsigned mask = (1u << M) - 1u, p0 = ((idx >> M) & mask) + off, p1 = (idx & mask) + off, sign = (idx >> (2u * M)) & 1u;
    const unsigned m0 = (p0 << 2) + track, m1 = (p1 << 2) + track;
    if (p1 < p0) { if (sign) { c[m0] -= 1.0f; c[m1] += 1.0f; } else { c[m0] += 1.0f; c[m1] -= 1.0f; } }
    else { if (sign) { c[m0] -= 1.0f; c[m1] -= 1.0f; } else { c[m0] += 1.0f; c[m1] += 1.0f; } }
}
static void pulses_3(unsigned idx, unsigned M, unsigned off, unsigned track, float *c) {
    unsigned j = off;
    if ((idx >> (2u * M - 1u)) & 1u) j += 1u << (M - 1u);
    pulses_2(idx & ((1u << (2u * M - 1u)) - 1u), M - 1u, j, track, c);
    pulses_1((idx >> (2u * M)) & ((1u << (M + 1u)) - 1u), M, off, track, c);
}
static void pulses_4_section(unsigned idx, unsigned off, unsigned track, float *c) {
    unsigned j = off;
    if ((idx >> 5) & 1u) j += 4;
    pulses_2(idx & 31u, 2, j, track, c);
    pulses_2((idx >> 6) & 127u, 3, off, track, c);
}
static void pulses_4(unsigned idx, unsigned track, float *c) {
    switch ((idx >> 14) & 3u) {
    case 0: pulses_4_section(idx, ((idx >> 13) & 1u) ? 8u : 0u, track, c); break;
    case 1: pulses_1(idx >> 10, 3, 0, track, c); pulses_3(idx, 3, 8, track, c); break;
    case 2: pulses_2(idx >> 7, 3, 0, track, c); pulses_2(idx, 3, 8, track, c); break;
    default: pulses_3(idx >> 4, 3, 0, track, c); pulses_1(idx, 3, 8, track, c); break;
    }
}
static void add_pulse_1p(unsigned idx, unsigned N, unsigned off, unsigned track, float *c) {
    unsigned pos = (idx & ((1u << N) - 1u)) + off;
    const unsigned neg = (idx >> N) & 1u;
    if (neg) pos += 16u;
    { const unsigned i = ((pos & 15u) << 2) + track; if ((pos & 16u) == 0u) c[i] += 1.0f; else c[i] -= 1.0f; }
}
/* the 64-sample algebraic codebook vector of an ACELP subframe; code_bits = the bits of the algebraic codebook of the core mode */
static void acelp_codevector(const uint32_t cb[8], unsigned code_bits, float code[64]) {
    unsigned t, i;
    memset(code, 0, 64 * sizeof(float));
    switch (code_bits) {
    case 12:
        for (t = 0; t < 4u; t += 2u) add_pulse_1p(cb[2u * (t / 2u) + 1u], 4, 0, 2u * cb[2u * (t / 2u)] + t / 2u, code);
        break;
    case 16: {
        unsigned skip, k = 0;
        skip = cb[k++] == 0u ? 1u : 3u;
        for (t = 0; t < 4u; t++) if (t != skip) add_pulse_1p(cb[k++], 4, 0, t, code);
        break; }
    case 20: for (t = 0; t < 4u; t++) pulses_1(cb[t], 4, 0, t, code); break;
    case 28:
        for (t = 0; t < 2u; t++) pulses_2(cb[t], 4, 0, t, code);
        for (t = 2; t < 4u; t++) pulses_1(cb[t], 4, 0, t, code);
        break;
    case 36: for (t = 0; t < 4u; t++) pulses_2(cb[t], 4, 0, t, code); break;
    case 44:
        for (t = 0; t < 2u; t++) pulses_3(cb[t], 4, 0, t, code);
        for (t = 2; t < 4u; t++) pulses_2(cb[t], 4, 0, t, code);
        break;
    case 52: for (t = 0; t < 4u; t++) pulses_3(cb[t], 4, 0, t, code); break;
    case 64: for (t = 0; t < 4u; t++) pulses_4((cb[t] << 14) + cb[t + 4u], t, code); break;
    default: break;
    }
    (void)i;
}
static void pitch_sharpen(float *x, int lag) { int i; for (i = lag; i < (int)LEN_SUBFR; i++) x[i] += x[i - lag] * 0.85f; }

/* adaptive codebook vector: the excitation delayed by the (fractional) pitch lag, interpolated; exc[-lag-...] is history, writes exc[0..64] */
static void adaptive_codebook(float *exc, int lag, int frac) {
    int i, j;
    const float *x0 = &exc[-lag];
    frac = -frac;
    if (frac < 0) { frac += 4; x0--; }
    for (j = 0; j < (int)LEN_SUBFR + 1; j++) {
        const float *x1 = x0++, *x2 = x1 + 1;
        const float *c1 = &sauti_usac_lpd_interpol_filt[frac], *c2 = &sauti_usac_lpd_interpol_filt[4 - frac];
        float s = 0.0f;
        for (i = 0; i < 16; i++, c1 += 4, c2 += 4) s += (*x1--) * (*c1) + (*x2++) * (*c2);
        exc[j] = s;
    }
}

typedef struct {
    float *synth_buf, *exc_buf, *pitch_gain;
    int *pitch;
    unsigned len_subfr, nsub_by2_m1;                     /* quarter length, (num_subfr_in_superfr / 2) - 1 */
} Bufs;

/* one ACELP quarter (4 subframes of 64): lp = the (num_subfr + 1) filters of the quarter, stability = smoothing factor */
static int acelp_quarter(SautiUsacLpdState *st, const SautiUsacLpdFrame *f, unsigned k, const float *lp, float stability, const Bufs *B, unsigned num_subfr) {
    static const unsigned code_bits[8] = { 20, 28, 36, 44, 52, 64, 12, 16 };
    const unsigned len_subfr = B->len_subfr, fac_length = len_subfr / 2u, core_mode = f->acelp_core_mode;
    const SautiUsacLpdAcelp *a = &f->acelp[k];
    float *synth_signal = B->synth_buf + len_subfr * k + MAX_PITCH + B->nsub_by2_m1 * LEN_SUBFR;
    float *xcit = B->exc_buf + len_subfr * k + EXC_PAD;
    float *ptr_gain = B->pitch_gain + k * num_subfr + B->nsub_by2_m1;
    int *ptr_pitch = B->pitch + k * num_subfr + B->nsub_by2_m1;
    float synth_temp[128 + 16] = { 0 }, x[128], xn2[2 * 128 + 16];
    const float *lp_cur;
    unsigned i, sub, subfr_nb = 0;
    int pitch_lag = 0, frac = 0, lag_min = 0, lag_max;
    const int i_off = (int)(((st->fscale * 34u) + 6400u) / 12800u) - 34;
    const int pitch_min = 34 + i_off, pitch_fr2 = 128 - i_off, pitch_fr1 = 160, pitch_max = 231 + 6 * i_off;

    if (st->mode_prev > 0) {                             /* TCX before: forward aliasing cancellation at the start of the quarter */
        int32_t iv[128];
        const SautiUsacLpdFac *fac = &f->fac[k];
        float tmp;
        if (!fac->present || fac->length != fac_length || !fac_values(fac, iv)) return 0;
        for (i = 0; i < fac_length; i++) x[i] = st->fac_gain * (float)iv[i];
        for (i = 0; i < fac_length / 4u; i++) x[i] *= st->fac_fd[i];
        dct4(x, xn2 + fac_length, (int)fac_length);
        for (i = 0; i < fac_length; i++) xn2[fac_length + i] *= 2.0f / (float)fac_length;
        memset(xn2, 0, fac_length * sizeof(float));
        synth_weighted_inplace(st->lp_prev, xn2 + fac_length, (int)fac_length);
        for (i = 0; i < 2u * fac_length; i++) xn2[i] += synth_signal[(int)i - (int)(2u * fac_length)];
        memcpy(synth_signal - fac_length, xn2 + fac_length, fac_length * sizeof(float));
        tmp = 0.0f;
        preemph(xn2, PREEMPH, (int)(2u * fac_length), tmp);
        {
            const float *a = st->lp_prev;
            const unsigned TTT = fac_length % LEN_SUBFR;
            if (TTT) { residual(a, &xn2[fac_length], &xcit[(int)fac_length - (int)(2u * fac_length)], (int)TTT, 1); a += 17; }
            residual(a, &xn2[fac_length + TTT], &xcit[(int)TTT - (int)fac_length], (int)LEN_SUBFR, (int)((fac_length + TTT) / LEN_SUBFR));
        }
    }

    for (i = 0; i < ORDER; i++) synth_temp[i] = synth_signal[(int)i - (int)ORDER] - PREEMPH * synth_signal[(int)i - (int)ORDER - 1];

    lp_cur = lp;
    for (sub = 0; sub < len_subfr; sub += LEN_SUBFR, subfr_nb++) {
        float code[64], post[64], pitch_gain, gain_code, innov_energy, pitch_energy, r_v, mean_ener_code, voicing, smooth_factor, gain0, gain_smooth, cpe;
        int idx, flag = (int)sub, ii;
        if (len_subfr == 256u && sub == 2u * LEN_SUBFR) flag = 0;
        idx = (int)a->acb_index[subfr_nb];
        if (flag == 0) {
            if (idx < (pitch_fr2 - pitch_min) * 4) { pitch_lag = pitch_min + idx / 4; frac = idx - (pitch_lag - pitch_min) * 4; }
            else if (idx < (pitch_fr2 - pitch_min) * 4 + (pitch_fr1 - pitch_fr2) * 2) {
                idx -= (pitch_fr2 - pitch_min) * 4;
                pitch_lag = pitch_fr2 + idx / 2; frac = (idx - (pitch_lag - pitch_fr2) * 2) * 2;
            } else { pitch_lag = idx + pitch_fr1 - (pitch_fr2 - pitch_min) * 4 - (pitch_fr1 - pitch_fr2) * 2; frac = 0; }
            lag_min = pitch_lag - 8; if (lag_min < pitch_min) lag_min = pitch_min;
            lag_max = lag_min + 15;
            if (lag_max > pitch_max) { lag_max = pitch_max; lag_min = lag_max - 15; }
        } else { pitch_lag = lag_min + idx / 4; frac = idx - (pitch_lag - lag_min) * 4; }
        if (pitch_lag < 1 || pitch_lag > (int)MAX_PITCH || frac < 0 || frac > 3) return 0;   /* an index outside the pitch range of the core rate */
        adaptive_codebook(&xcit[sub], pitch_lag, frac);
        mean_ener_code = (float)a->mean_energy * 12.0f + 18.0f;
        if (a->ltp_filtering[subfr_nb] == 0u) {
            for (ii = 0; ii < (int)LEN_SUBFR; ii++) code[ii] = (float)(0.18 * xcit[ii - 1 + (int)sub] + 0.64 * xcit[ii + (int)sub] + 0.18 * xcit[ii + 1 + (int)sub]);
            memcpy(&xcit[sub], code, LEN_SUBFR * sizeof(float));
        }
        acelp_codevector(a->icb_index[subfr_nb], code_bits[core_mode], code);
        preemph(code, TILT_CODE, (int)LEN_SUBFR, 0.0f);
        ii = pitch_lag; if (frac > 2) ii++;
        if (ii >= 0) pitch_sharpen(code, ii);
        {   /* gains */
            float en = 0.01f, est;
            const unsigned gi = a->gain[subfr_nb];
            for (ii = 0; ii < (int)LEN_SUBFR; ii++) en += code[ii] * code[ii];
            innov_energy = en;
            est = mean_ener_code - (float)(10.0 * log10(en / (float)LEN_SUBFR));
            est = (float)pow(10.0, 0.05 * est);
            pitch_gain = sauti_usac_lpd_gain_table[gi][0];
            gain_code = sauti_usac_lpd_gain_table[gi][1] * est;
        }
        pitch_energy = 0.0f;
        for (ii = 0; ii < (int)LEN_SUBFR; ii++) pitch_energy += xcit[ii + (int)sub] * xcit[ii + (int)sub];
        pitch_energy *= pitch_gain * pitch_gain;
        innov_energy *= gain_code * gain_code;
        r_v = (pitch_energy - innov_energy) / (pitch_energy + innov_energy);
        for (ii = 0; ii < (int)LEN_SUBFR; ii++) post[ii] = pitch_gain * xcit[ii + (int)sub];
        for (ii = 0; ii < (int)LEN_SUBFR; ii++) xcit[ii + (int)sub] = pitch_gain * xcit[ii + (int)sub] + gain_code * code[ii];
        ii = pitch_lag; if (frac > 2) ii++;
        if (ii > pitch_max) ii = pitch_max;
        *ptr_pitch++ = ii;
        *ptr_gain++ = pitch_gain;
        voicing = (float)(0.5 * (1.0 - r_v));
        smooth_factor = stability * voicing;
        gain0 = gain_code;
        if (gain0 < st->gain_threshold) { gain0 = (float)(gain0 * 1.19); if (gain0 > st->gain_threshold) gain0 = st->gain_threshold; }
        else { gain0 = (float)(gain0 / 1.19); if (gain0 < st->gain_threshold) gain0 = st->gain_threshold; }
        st->gain_threshold = gain0;
        gain_smooth = (float)(smooth_factor * gain0 + (1.0 - smooth_factor) * gain_code);
        for (ii = 0; ii < (int)LEN_SUBFR; ii++) code[ii] *= gain_smooth;
        cpe = (float)(0.125 * (1.0 + r_v));
        post[0] += code[0] - cpe * code[1];
        for (ii = 1; ii < (int)LEN_SUBFR - 1; ii++) post[ii] += code[ii] - cpe * (code[ii - 1] + code[ii + 1]);
        post[LEN_SUBFR - 1] += code[LEN_SUBFR - 1] - cpe * code[LEN_SUBFR - 2];
        synth_mem(lp_cur, post, &synth_signal[sub], (int)LEN_SUBFR, synth_temp);
        memcpy(synth_temp, &synth_signal[sub + LEN_SUBFR - ORDER], ORDER * sizeof(float));
        lp_cur += 17;
    }
    deemph(synth_signal, (int)len_subfr, synth_signal[-1]);
    memset(synth_temp + 16, 0, 128 * sizeof(float));
    synth_inplace(lp_cur, synth_temp + 16, 128);         /* zero input response of the filter after the quarter */
    lp_cur -= 2 * 17;
    memcpy(st->lp_prev, lp_cur, 2 * 17 * sizeof(float));
    memcpy(st->exc_prev, synth_signal + len_subfr - (1 + fac_length), (1 + fac_length) * sizeof(float));
    memcpy(st->exc_prev + 1 + fac_length, synth_temp + 16, fac_length * sizeof(float));
    deemph(st->exc_prev + 1 + fac_length, (int)fac_length, synth_signal[len_subfr - 1]);
    return 1;
}

/* ---- TCX ---- */
static float random_sign(unsigned *seed) {
    *seed = (unsigned)(((uint64_t)*seed * 69069u) + 5u);
    return (*seed & 0x10000u) ? -1.0f : 1.0f;
}
static void low_freq_deemphasis(float *x, int lg, float *gains) {
    const int k = 8, i_max = lg / 4;
    int i, j;
    float max = 0.01f, factor = 0.1f, rm;
    for (i = 0; i < i_max; i += k) { rm = 0.01f; for (j = i; j < i + k; j++) rm += x[j] * x[j]; if (rm > max) max = rm; }
    for (i = 0; i < i_max; i += k) {
        rm = 0.01f;
        for (j = i; j < i + k; j++) rm += x[j] * x[j];
        rm = (float)sqrt(rm / max);
        if (rm > factor) factor = rm;
        for (j = i; j < i + k; j++) x[j] *= factor;
        gains[i / k] = factor;
    }
}
/* 1 / |A(e^jw)| on the grid w = pi (2k + 1) / (2 * 2 * lg), k < lg  (the LPC "spectrum" of one TCX quarter) */
static void lpc_to_gains(const float *coeff, float *gains, int lg) {
    const int n = 2 * lg;
    int k, i;
    for (k = 0; k < lg; k++) {
        double re = 0.0, im = 0.0;
        for (i = 0; i <= 16; i++) { const double ang = PI_D * i * (2.0 * k + 1.0) / n; re += coeff[i] * cos(ang); im -= coeff[i] * sin(ang); }
        gains[k] = (float)(1.0 / (sqrt(re * re + im * im) + 1.1920929e-7));
    }
}
static void noise_shaping(float *r, int lg, int M, const float *g1, const float *g2) {
    const int k = lg / M;
    float rr[1024], prev = 0.0f, a = 0.0f, b = 0.0f;
    int i;
    memcpy(rr, r, (size_t)lg * sizeof(float));
    for (i = 0; i < lg; i++) {
        if ((i % k) == 0) { const float s = g1[i / k] + g2[i / k]; a = 2.0f * g1[i / k] * g2[i / k] / s; b = (g2[i / k] - g1[i / k]) / s; }
        rr[i] = a * rr[i] + b * prev;
        prev = rr[i];
    }
    memcpy(r, rr, (size_t)lg * sizeof(float));                /* the reference decoder stores this in a permuted order (even samples, then the odd ones reversed) for its FFT based transform: not needed here */
}

static int tcx_quarter(SautiUsacLpdState *st, const SautiUsacLpdFrame *f, unsigned k, const float *lp, unsigned lg, unsigned ccfl, unsigned *seed, const Bufs *B) {
    const unsigned len_subfr = B->len_subfr, fac_length = len_subfr / 2u;
    const unsigned fac_prev = st->mode_prev == -2 ? ccfl / 16u : fac_length;
    const unsigned off_quant = k ? f->tcx_len[0] + (k > 1u ? f->tcx_len[1] : 0u) + (k > 2u ? f->tcx_len[2] : 0u) : 0u;
    float x[1024], xn_buf[1024 + 256], g1[256], g2[256], alfd[128], ap[17], gain_tcx, noise_level, energy, tmp, xn1[256], facwin[256], buf[ORDER + 256 + 1];
    float *exc = B->exc_buf + len_subfr * k + EXC_PAD, *synth = B->synth_buf + len_subfr * k + MAX_PITCH + B->nsub_by2_m1 * LEN_SUBFR, *xn = xn_buf + fac_length;
    unsigned i;
    const int32_t *q = f->quant + off_quant;
    (void)lp;

    if (st->mode_prev != 0) {
        if (st->mode_prev > 0) for (i = 0; i < 2u * fac_prev; i++) st->exc_prev[i + fac_length - fac_prev + 1u] *= sine_win(2u * fac_prev, 2u * fac_prev - 1u - i);
        for (i = 0; i < fac_length - fac_prev; i++) st->exc_prev[i + fac_length + fac_prev + 1u] = 0.0f;
    }
    noise_level = 0.0625f * (8.0f - (float)f->noise_factor[k]);
    for (i = 0; i < lg; i++) x[i] = (float)q[i];
    for (i = lg / 6u; i < lg; i += 8u) {
        unsigned kk, max_k = lg < i + 8u ? lg : i + 8u;
        float t = 0.0f;
        for (kk = i; kk < max_k; kk++) t += (float)q[kk] * (float)q[kk];
        if (t == 0.0f) for (kk = i; kk < max_k; kk++) x[kk] = noise_level * random_sign(seed);
    }
    low_freq_deemphasis(x, (int)lg, alfd);
    weight_filter(lp + 17, ap, 0.92f);
    lpc_to_gains(ap, g1, (int)len_subfr / 4);
    weight_filter(lp + 34, ap, 0.92f);
    lpc_to_gains(ap, g2, (int)len_subfr / 4);
    energy = 0.01f;
    for (i = 0; i < lg; i++) energy += x[i] * x[i];
    tmp = (float)sqrt(energy) / (float)lg;
    gain_tcx = (float)pow(10.0, (double)f->global_gain[k] / 28.0) / (tmp * 2.0f);
    noise_shaping(x, (int)lg, (int)len_subfr / 4, g1, g2);
    imdct_main(x, xn_buf, (int)(2u * fac_length), (int)(lg - 2u * fac_length));
    for (i = 0; i < lg + 2u * fac_length; i++) xn_buf[i] *= 2.0f / (float)lg;
    st->fac_gain = gain_tcx * 0.5f * (float)sqrt((float)fac_length / (float)lg);
    for (i = 0; i < fac_length / 4u; i++) st->fac_fd[i] = alfd[i * lg / (8u * fac_length)];
    if (st->mode_prev == 0) {                            /* ACELP before: the FAC data of this quarter cancels the aliasing of the overlap */
        int32_t iv[128];
        const SautiUsacLpdFac *fac = &f->fac[k];
        float *fx = x;
        if (!fac->present || fac->length != fac_length || !fac_values(fac, iv)) return 0;
        for (i = 0; i < fac_prev; i++) { facwin[i] = sine_win(2u * fac_prev, i) * sine_win(2u * fac_prev, 2u * fac_prev - 1u - i); facwin[fac_prev + i] = 1.0f - sine_win(2u * fac_prev, fac_prev + i) * sine_win(2u * fac_prev, fac_prev + i); }
        for (i = 0; i < fac_length; i++) fx[i] = st->fac_gain * (float)iv[i];
        for (i = 0; i < fac_length / 4u; i++) fx[i] *= st->fac_fd[i];
        dct4(fx, xn1, (int)fac_length);
        for (i = 0; i < fac_length; i++) xn1[i] *= 2.0f / (float)fac_length;
        memset(xn1 + fac_length, 0, fac_length * sizeof(float));
        weight_filter(lp + 17, ap, 0.92f);
        synth_mem(ap, xn1, xn1, (int)(2u * fac_length), xn1 + fac_length);
        for (i = 0; i < fac_length; i++) xn1[i] += st->exc_prev[1u + fac_length + i] * facwin[fac_length + i] + st->exc_prev[fac_length - i] * facwin[fac_length - 1u - i];
    }
    for (i = 0; i < lg + 2u * fac_length; i++) xn_buf[i] *= gain_tcx;
    for (i = 0; i < 2u * fac_prev; i++) xn_buf[i + fac_length - fac_prev] *= sine_win(2u * fac_prev, i);
    for (i = 0; i < fac_length - fac_prev; i++) xn_buf[i] = 0.0f;
    if (st->mode_prev != 0) for (i = fac_length - fac_prev; i < fac_length + fac_prev; i++) xn_buf[i] += st->exc_prev[1u + i];
    else for (i = fac_length - fac_prev; i < fac_length + fac_prev; i++) xn_buf[i + fac_length] += xn1[i];
    memcpy(st->exc_prev, xn_buf + lg - 1u, (1u + 2u * fac_length) * sizeof(float));
    for (i = 0; i < 2u * fac_length; i++) xn_buf[i + lg] *= sine_win(2u * fac_length, 2u * fac_length - 1u - i);
    if (st->mode_prev != 0) {
        const float *a = st->lp_prev;
        const unsigned TTT = fac_length % LEN_SUBFR;
        memcpy(synth - fac_prev, xn_buf + fac_length - fac_prev, fac_prev * sizeof(float));
        for (i = 0; i < ORDER + fac_length; i++) buf[i] = synth[(int)i - (int)ORDER - (int)fac_length] - PREEMPH * synth[(int)i - (int)ORDER - (int)fac_length - 1];
        if (TTT) residual(a, &buf[ORDER], &exc[-(int)fac_length], (int)TTT, 1);
        residual(a, &buf[ORDER + TTT], &exc[(int)TTT - (int)fac_length], (int)LEN_SUBFR, (int)((fac_length - TTT) / LEN_SUBFR));
    }
    memcpy(synth, xn, lg * sizeof(float));
    {
        float mem;
        memcpy(xn - ORDER - 1, synth - ORDER - 1, (ORDER + 1) * sizeof(float));
        mem = xn[-(int)ORDER - 1];
        preemph(xn - ORDER, PREEMPH, (int)(ORDER + lg), mem);
    }
    {
        const float *a = lp + 34;
        residual(a, xn, exc, (int)lg, 1);
        memcpy(st->lp_prev, a, 17 * sizeof(float));
        memcpy(st->lp_prev + 17, a, 17 * sizeof(float));
    }
    return 1;
}

/* ---- long-term (bass) post-filter ---- */
static int bass_postfilter(const float *synth_sig, const int *pitch, const float *pitch_gain, float *out, int len_fr, int len2, float *bpf_prev) {
    float noise_buf[FILTER_DELAY + 2 * 64];
    float *n1 = noise_buf + FILTER_DELAY, *n2 = noise_buf + FILTER_DELAY + 64;
    int i, j, sf = 0, nsub;
    memcpy(out, synth_sig - LEN_SUBFR, (size_t)len_fr * sizeof(float));
    if (len_fr % 64) memset(out + len_fr, 0, (size_t)(64 - len_fr % 64) * sizeof(float));
    for (nsub = 0; nsub < len_fr; nsub += 64, sf++) {
        int lag = pitch[sf], lg;
        float gain = pitch_gain[sf], xe, xy, ye, nc, tmp, energy, alpha;
        const float *x, *y;
        if (((lag >> 1) + 96 - nsub) > (int)MAX_PITCH) return 0;
        if (gain > 1.0f) gain = 1.0f;
        if (gain < 0.0f) gain = 0.0f;
        x = &synth_sig[nsub - 96]; y = &synth_sig[nsub - lag / 2 - 96];
        xe = xy = ye = 0.01f;
        for (i = 0; i < 64 + 96; i++) { xe += x[i] * x[i]; xy += x[i] * y[i]; ye += y[i] * y[i]; }
        nc = xy / (float)sqrt(xe * ye);
        if (nc > 0.95f) lag >>= 1;
        lg = len_fr + len2 - lag - nsub;
        if (lg < 0) lg = 0;
        if (lg > 64) lg = 64;
        if (lag > (int)MAX_PITCH) return 0;
        if (gain > 0.0f) {
            if (lg > 0) {
                tmp = 0.01f; energy = 0.01f;
                for (i = 0; i < lg; i++) tmp += synth_sig[i + nsub] * synth_sig[i + nsub];
                for (i = 0; i < lg; i++) energy += synth_sig[i + nsub + lag] * synth_sig[i + nsub + lag];
                tmp = (float)sqrt(tmp / energy);
                if (tmp < gain) gain = tmp;
            }
            alpha = 0.5f * gain;
            for (i = 0; i < lg; i++) n2[i] = alpha * (synth_sig[i + nsub] - 0.5f * synth_sig[i + nsub - lag] - 0.5f * synth_sig[i + nsub + lag]);
            for (i = lg; i < 64; i++) n2[i] = alpha * (synth_sig[i + nsub] - synth_sig[i + nsub - lag]);
        } else memset(n2, 0, 64 * sizeof(float));
        memcpy(noise_buf, bpf_prev, (FILTER_DELAY + 64) * sizeof(float));
        memcpy(bpf_prev, noise_buf + 64, (FILTER_DELAY + 64) * sizeof(float));
        for (i = 0; i < 64; i++) {
            tmp = sauti_usac_lpd_bpf_fir[0] * n1[i];
            for (j = 1; j <= FILTER_DELAY; j++) tmp += sauti_usac_lpd_bpf_fir[j] * (n1[i - j] + n1[i + j]);
            out[i + nsub] -= tmp;
        }
    }
    return 1;
}


/* ---- LPD -> FD ---- */
static int fd_fac_signal(const SautiUsacLpdState *st, const SautiUsacLpdFac *fac, unsigned lfac, unsigned center, float *tmp);
/* The part of the LPD synthesis that is still to be output when an FD frame follows (the look-ahead of the post-filter) and the memory that the FAC of the FD frame needs.  ccfl = 1024 only. */
static void lpd_overlap(const SautiUsacLpdState *st, unsigned ccfl, float *ovl) {
    const unsigned fac = ccfl / 8u, lpd_sbf_len = 2u * (ccfl / 256u), delay = (lpd_sbf_len - 1u) * LEN_SUBFR;       /* ixheaacd_lpd_dec_update: 448 / fac 128 at 1024, 320 / 96 at 768 */
    unsigned i;
    for (i = 0; i < LEN_SUBFR + delay; i++) ovl[i] = st->synth_prev[MAX_PITCH - LEN_SUBFR + i];
    for (i = 0; i < 2u * fac; i++) ovl[LEN_SUBFR + delay - fac + i] = st->exc_prev[i + 1u];
    for (i = LEN_SUBFR + delay + fac; i < ccfl; i++) ovl[i] = 0.0f;
}
/* the long-term post-filter over the start of the FD frame (out[0..704) are replaced) */
static int bpf_continue(SautiUsacLpdState *st, int is_short, float *out) {
    float synth_buf[MAX_PITCH + SAUTI_LPD_SYNTH_DELAY_MAX + 1024 + 64], sig[1024 + 64], gain[16];
    int pitch[16];
    const unsigned by2 = 7u, sf = 8u;
    unsigned i, k;
    memset(synth_buf, 0, sizeof synth_buf);
    memcpy(synth_buf, st->synth_prev, (MAX_PITCH + 448u) * sizeof(float));
    memcpy(synth_buf + MAX_PITCH - LEN_SUBFR, out, 1024u * sizeof(float));      /* the reference copies synth_delay + ccfl + 64 samples from the frame buffer: what lies behind the frame is not part of the output */
    for (i = 0; i < by2; i++) { pitch[i] = st->pitch_prev[i]; gain[i] = st->gain_prev[i]; }
    for (i = by2; i < sf + 3u; i++) { pitch[i] = 64; gain[i] = 0.0f; }
    if (st->mode_prev == 0) { pitch[by2] = pitch[by2 - 1u]; gain[by2] = gain[by2 - 1u]; if (!is_short) { pitch[by2 + 1u] = pitch[by2]; gain[by2 + 1u] = gain[by2]; } }
    {
        const float *synth = synth_buf + MAX_PITCH;
        for (i = 0; i < by2 + 2u; i++) {
            const int tp = pitch[i];
            if ((int)(i * LEN_SUBFR + MAX_PITCH) < tp) return 0;
            if ((int)(i * LEN_SUBFR + MAX_PITCH) - tp >= 1883 || i * LEN_SUBFR + LEN_SUBFR > 1024u || (int)(i * LEN_SUBFR + LEN_SUBFR) - tp > 1024) return 0;
            if (gain[i] > 0.0f) {
                float corr = 0.0f, en = 1e-6f;
                for (k = 0; k < LEN_SUBFR; k++) { corr += synth[i * LEN_SUBFR + k] * synth[(int)(i * LEN_SUBFR) - tp + (int)k]; en += synth[(int)(i * LEN_SUBFR) - tp + (int)k] * synth[(int)(i * LEN_SUBFR) - tp + (int)k]; }
                gain[i] = corr / en;
            }
        }
        if (!bass_postfilter(synth, pitch, gain, sig, (int)((sf + 2u) * LEN_SUBFR + LEN_SUBFR), (int)(1024u - (sf + 4u) * LEN_SUBFR), st->bpf_prev)) return 0;
    }
    memcpy(out, sig, ((sf + 2u) * LEN_SUBFR + LEN_SUBFR) * sizeof(float));
    return 1;
}

int sauti_usac_lpd_fd_frame(SautiUsacLpdState *st, unsigned ccfl, unsigned seq, const float *x, const SautiUsacLpdFac *fac, float *out) {
    const unsigned lfac = ccfl / 8u, n_flat = (ccfl - 2u * lfac) / 2u, n_trans = 2u * lfac;
    float ovl[1024];
    unsigned i;
    if (ccfl != 1024u && ccfl != 768u) return -1;
    if (st->fscale < 6000u || st->fscale > 24000u) return -1;
    lpd_overlap(st, ccfl, ovl);
    if (seq == 0u) {                                     /* ONLY_LONG after LPD: the reference overlaps the whole frame with a full sine slope and ignores the FAC data (LONG_START after LPD is STOP_START, LONG_STOP stays: both take the path below) */
        for (i = 0; i < ccfl; i++) out[i] = x[i] * sine_win(ccfl, i) + ovl[i] * sine_win(ccfl, ccfl - 1u - i);
        if (!bpf_continue(st, 0, out)) return 0;
        st->prev_lpd = 0;
        return 1;
    }
    for (i = 0; i < n_flat + lfac; i++) out[i] = ovl[i];
    if (fac && fac->present) {                           /* the aliasing cancellation signal of the FD frame */
        float tmp[256];
        if (!fd_fac_signal(st, fac, lfac, ccfl / 8u, tmp)) return 0;
        for (i = n_flat + lfac; i < n_flat + n_trans; i++) out[i] = x[i] * sine_win(n_trans, lfac + (i - (n_flat + lfac))) + tmp[i - (n_flat + lfac)];
        for (; i < n_flat + 3u * lfac; i++) out[i] = x[i] + tmp[i - (n_flat + lfac)];
        for (; i < ccfl; i++) out[i] = x[i];
    } else {
        for (i = n_flat; i < ccfl / 2u; i++) out[i] = x[i] * sine_win(n_trans, i - n_flat) + ovl[i] * sine_win(n_trans, n_trans - 1u - (i - n_flat));
        for (i = ccfl / 2u; i < n_flat + n_trans; i++) out[i] = x[i] * sine_win(n_trans, i - n_flat) + ovl[i] * sine_win(n_trans, n_trans - 1u - (i - n_flat));
        for (; i < ccfl; i++) out[i] = x[i];
        for (i = 0; i < n_flat; i++) out[i] = ovl[i];
    }
    if (!bpf_continue(st, 0, out)) return 0;
    st->prev_lpd = 0;
    return 1;
}

/* the aliasing cancellation signal of an FD frame after an LPD frame, 2 * lfac samples (PCM scale); lfac = ccfl / 8 (long) or ccfl / 16 (eight short windows) */
static int fd_fac_signal(const SautiUsacLpdState *st, const SautiUsacLpdFac *fac, unsigned lfac, unsigned center, float *tmp) {
    int32_t iv[192];
    float fx[192] = { 0 }, y[2 * 192 + 16];
    const float gain = (float)pow(10.0, (double)fac->gain / 28.0);
    unsigned i;
    if (fac->length != lfac || !fac_values(fac, iv)) return 0;
    for (i = 0; i < lfac; i++) fx[i] = (float)iv[i];
    memset(y, 0, sizeof y);
    dct4(fx, y + 16, (int)lfac);
    synth_weighted_inplace(st->lp_prev + 17, y + 16, (int)(2u * lfac));
    for (i = 0; i < lfac; i++) {
        const float w0 = sine_win(2u * lfac, lfac - 1u - i) * sine_win(2u * lfac, lfac + i), w1 = 1.0f - sine_win(2u * lfac, lfac + i) * sine_win(2u * lfac, lfac + i);
        tmp[i] = y[16 + i] * gain + st->exc_prev[center + 1u + i] * w1 + st->exc_prev[center - i] * w0;     /* center = ccfl / 8 = the middle of the stored excitation (128 at 1024, 96 at 768), also for the eight short windows (lfac = ccfl / 16) */
        tmp[lfac + i] = y[16 + lfac + i] * gain;
    }
    return 1;
}

int sauti_usac_lpd_fd_short_begin(const SautiUsacLpdState *st, unsigned ccfl, const SautiUsacLpdFac *fac, float *ovl, float *facsig) {
    if ((ccfl != 1024u && ccfl != 768u) || st->fscale < 6000u || st->fscale > 24000u) return -1;
    lpd_overlap(st, ccfl, ovl);
    memset(facsig, 0, 128 * sizeof(float));
    if (fac && fac->present && !fd_fac_signal(st, fac, ccfl / 16u, ccfl / 8u, facsig)) return 0;
    return 1;
}
int sauti_usac_lpd_fd_finish(SautiUsacLpdState *st, int is_short, float *out) {
    if (!bpf_continue(st, is_short, out)) return 0;
    st->prev_lpd = 0;
    return 1;
}

void sauti_usac_lpd_state_init(SautiUsacLpdState *st, unsigned core_rate) {
    memset(st, 0, sizeof *st);
    st->mode_prev = -1;
    st->fscale = core_rate;
    {
        unsigned i;
        for (i = 0; i < 7u; i++) st->pitch_prev[i] = 64;
        sauti_usac_lpd_lsf_to_lsp(sauti_usac_lpd_lsf_init, st->lsp_old);
        memcpy(st->lsf_prev, sauti_usac_lpd_lsf_init, sizeof st->lsf_prev);
    }
}

int sauti_usac_lpd_decode(SautiUsacLpdState *st, const SautiUsacLpdFrame *f, unsigned ccfl, unsigned *noise_seed, const float *fd_tail, int fd_last_short, float *out) {
    const unsigned len_subfr = ccfl / 4u, num_subfr = len_subfr / LEN_SUBFR, num_subfr_sf = 4u * num_subfr, by2 = num_subfr_sf / 2u - 1u, synth_delay = by2 * LEN_SUBFR;
    float synth_buf[MAX_PITCH + SAUTI_LPD_SYNTH_DELAY_MAX + 1024 + 64], exc_buf[EXC_PAD + 1024 + 64];
    float lsf[5][16], lsp_cur[16], lsf_cur[16], a[(8 + 1) * 17 + 17], pitch_gain[48];
    int pitch[48];
    unsigned i, k = 0;
    Bufs B;
    if (ccfl != 1024u && ccfl != 768u) return 0;
    if (st->fscale < 6000u || st->fscale > 24000u) return -1;          /* the pitch ranges and buffers of the LPD core are defined for core rates of 6 - 24 kHz (FAC_FSCALE_MIN / MAX) */
    if (!st->prev_lpd) {                                  /* the first LPD frame after an FD frame (or at the start): the state of the LPD core starts from the overlap of the FD frame */
        const unsigned fac = fd_last_short ? ccfl / 16u : len_subfr / 2u;
        unsigned q;
        st->mode_prev = fd_last_short ? -2 : -1;
        for (q = 0; q < 7u; q++) { st->pitch_prev[q] = 64; st->gain_prev[q] = 0.0f; }
        st->bpf_active_prev = 0;
        memset(st->lp_prev, 0, sizeof st->lp_prev); memset(st->xcit_prev, 0, sizeof st->xcit_prev); memset(st->synth_prev, 0, sizeof st->synth_prev); memset(st->bpf_prev, 0, sizeof st->bpf_prev);
        st->gain_threshold = 0.0f;
        if (fd_tail) memcpy(st->overlap, fd_tail, ccfl * sizeof(float)); else memset(st->overlap, 0, sizeof st->overlap);
        for (q = 0; q < 2u * fac; q++) st->overlap[ccfl / 2u - fac + q] *= sine_win(2u * fac, 2u * fac - 1u - q);   /* the slope towards the LPD core */
        for (q = 0; q < ccfl / 2u - fac; q++) st->overlap[ccfl / 2u + fac + q] = 0.0f;
        memset(st->exc_prev, 0, sizeof st->exc_prev);
        for (q = 0; q < 2u * fac + 1u; q++) st->exc_prev[len_subfr / 2u - fac + q] = st->overlap[q + ccfl / 2u - fac - 1u];
    }
    memset(synth_buf, 0, sizeof synth_buf); memset(exc_buf, 0, sizeof exc_buf);
    memcpy(synth_buf, st->synth_prev, (MAX_PITCH + synth_delay) * sizeof(float));
    memcpy(exc_buf, st->xcit_prev, EXC_PAD * sizeof(float));
    for (i = 0; i < by2; i++) { pitch[i] = st->pitch_prev[i]; pitch_gain[i] = st->gain_prev[i]; }
    for (i = 0; i < num_subfr_sf; i++) { pitch[i + by2] = 64; pitch_gain[i + by2] = 0.0f; }
    B.synth_buf = synth_buf; B.exc_buf = exc_buf; B.pitch_gain = pitch_gain; B.pitch = pitch; B.len_subfr = len_subfr; B.nsub_by2_m1 = by2;

    if (!f->first_lpd_flag) sauti_usac_lpd_lsp_to_lsf(st->lsp_old, lsf[0]);
    if (!sauti_usac_lpd_lsf_dequant(f, lsf)) return 0;
    if (f->first_lpd_flag) {
        memcpy(st->lsf_prev, lsf[0], sizeof st->lsf_prev);
        sauti_usac_lpd_lsf_to_lsp(st->lsf_prev, st->lsp_old);
    }
    if (f->first_lpd_flag && (f->mod[0] == 0u || f->mod[1] == 0u || (f->mod[2] == 0u && len_subfr != 256u))) {     /* ACELP somewhere in the first half after an FD frame: the overlap becomes the start of the synthesis and of the excitation history */
        float a0[17], fdb[16 + 512], res[512], *fds = fdb + 16;
        const unsigned fac_len = f->short_fac_flag ? len_subfr * 4u / 16u : len_subfr / 2u;
        unsigned q, num;
        sauti_usac_lpd_lsp_to_lpc(st->lsp_old, a0);
        memcpy(st->lp_prev, a0, sizeof a0); memcpy(st->lp_prev + 17, a0, sizeof a0);
        if (f->mod[0] == 0u) {                           /* the FAC data of the frame carry the aliasing cancellation of the FD frame's tail */
            int32_t iv[192];
            float fx[192] = { 0 }, gain;
            if (!f->fac_end.present || f->fac_end.length != fac_len || !fac_values(&f->fac_end, iv)) return 0;
            gain = (float)pow(10.0, (double)f->fac_end.gain / 28.0);
            for (q = 0; q < fac_len; q++) fx[q] = (float)iv[q];
            { float y[192 + 16]; memset(y, 0, 16 * sizeof(float)); dct4(fx, y + 16, (int)fac_len);
              synth_weighted_inplace(a0, y + 16, (int)fac_len);
              for (q = 0; q < fac_len; q++) st->overlap[ccfl / 2u - fac_len + q] += gain * y[16 + q]; }
            memset(st->overlap + ccfl / 2u, 0, fac_len * sizeof(float));
        }
        memset(fdb, 0, sizeof fdb);
        for (q = 0; q < 2u * len_subfr; q++) fds[q] = st->overlap[q];
        memcpy(synth_buf + MAX_PITCH + synth_delay - 2u * len_subfr, fds, 2u * len_subfr * sizeof(float));
        preemph(fds, PREEMPH, (int)(2u * len_subfr), 0.0f);
        for (q = 0; q < 2u * len_subfr / LEN_SUBFR; q++) residual(a0, fds + q * LEN_SUBFR, res + q * LEN_SUBFR, (int)LEN_SUBFR, 1);
        num = (f->mod[0] != 0u && (len_subfr == 256u || f->mod[1] != 0u)) ? len_subfr : 2u * len_subfr;
        if (num > EXC_PAD) num = EXC_PAD;
        memcpy(exc_buf + EXC_PAD - num, res + 2u * len_subfr - num, num * sizeof(float));
    }

    while (k < 4u) {
        unsigned mode = f->mod[k];
        float stability, d = 0.0f;
        if (mode == 0u || mode == 1u) memcpy(lsf_cur, &lsf[0][0] + (k + 1u) * 16u, sizeof lsf_cur);
        else if (mode == 2u) memcpy(lsf_cur, &lsf[0][0] + (k + 2u) * 16u, sizeof lsf_cur);
        else memcpy(lsf_cur, &lsf[0][0] + (k + 4u) * 16u, sizeof lsf_cur);
        sauti_usac_lpd_lsf_to_lsp(lsf_cur, lsp_cur);
        for (i = 0; i < 16u; i++) d += (lsf_cur[i] - st->lsf_prev[i]) * (lsf_cur[i] - st->lsf_prev[i]);
        stability = 1.25f - d / 400000.0f;
        if (stability > 1.0f) stability = 1.0f;
        if (stability < 0.0f) stability = 0.0f;
        if (st->mode_prev == 0 && mode > 0u && (k != 0u || st->bpf_active_prev == 1)) {
            const unsigned ii = k * num_subfr + by2;
            pitch[ii + 1] = pitch[ii] = pitch[ii - 1];
            pitch_gain[ii + 1] = pitch_gain[ii] = pitch_gain[ii - 1];
        }
        if (mode == 0u) {
            sauti_usac_lpd_lpc_interpolate(st->lsp_old, lsp_cur, a, num_subfr);
            if (!acelp_quarter(st, f, k, a, stability, &B, num_subfr)) return 0;
            if (st->mode_prev != 0 && f->bpf_control_info) {
                const unsigned ii = k * num_subfr + by2;
                pitch[ii - 1] = pitch[ii]; pitch_gain[ii - 1] = pitch_gain[ii];
                if (st->mode_prev != -2) { pitch[ii - 2] = pitch[ii]; pitch_gain[ii - 2] = pitch_gain[ii]; }
            }
            k++;
        } else {
            const unsigned lg = len_subfr << (mode - 1u), nsub = mode == 1u ? num_subfr : (mode == 2u ? num_subfr_sf / 2u : num_subfr_sf);
            sauti_usac_lpd_lpc_tcx_set(st->lsp_old, lsp_cur, a, nsub);
            if (!tcx_quarter(st, f, k, a, lg, ccfl, noise_seed, &B)) return 0;
            k += 1u << (mode - 1u);
        }
        st->mode_prev = (int)mode;
        memcpy(st->lsp_old, lsp_cur, sizeof lsp_cur);
        memcpy(st->lsf_prev, lsf_cur, sizeof lsf_cur);
    }
    memcpy(st->xcit_prev, exc_buf + ccfl, EXC_PAD * sizeof(float));
    memcpy(st->synth_prev, synth_buf + ccfl, (MAX_PITCH + synth_delay) * sizeof(float));
    if (!f->bpf_control_info) {
        if (f->mod[0] != 0u && st->bpf_active_prev) for (i = 2; i < num_subfr_sf; i++) pitch_gain[by2 + i] = 0.0f;
        else for (i = 0; i < num_subfr_sf; i++) pitch_gain[by2 + i] = 0.0f;
    }
    st->bpf_active_prev = (int)f->bpf_control_info;
    for (i = 0; i < by2; i++) { st->pitch_prev[i] = pitch[num_subfr_sf + i]; st->gain_prev[i] = pitch_gain[num_subfr_sf + i]; }
    {
        const float *synth = synth_buf + MAX_PITCH;
        for (i = 0; i < num_subfr_sf; i++) {
            int tp = pitch[i];
            if (pitch_gain[i] > 0.0f) {
                float corr = 0.0f, en = 1e-6f;
                unsigned j;
                if (((i * LEN_SUBFR) + LEN_SUBFR) > 1024u || (int)(((i * LEN_SUBFR) + LEN_SUBFR)) - tp > 1024) return 0;
                for (j = 0; j < LEN_SUBFR; j++) { corr += synth[i * LEN_SUBFR + j] * synth[(int)(i * LEN_SUBFR) - tp + (int)j]; en += synth[(int)(i * LEN_SUBFR) - tp + (int)j] * synth[(int)(i * LEN_SUBFR) - tp + (int)j]; }
                pitch_gain[i] = corr / en;
            }
        }
        {
            float fs[1024 + 64];
            if (!bass_postfilter(synth, pitch, pitch_gain, fs, (int)ccfl, f->mod[3] == 0u ? (int)synth_delay : (int)synth_delay - (int)len_subfr / 2, st->bpf_prev)) return 0;
            if (!st->prev_lpd) {                          /* the first LPD frame after the FD frame: its first (ccfl / 2 - lfac) samples are the tail of the FD frame, the overlap of the slope is part of the synthesis */
                const unsigned lfac = fd_last_short ? ccfl / 16u : ccfl / 8u, zero = ccfl / 2u - lfac - (f->mod[0] == 0u ? LEN_SUBFR : 0u);
                for (i = 0; i < zero; i++) fs[i] = 0.0f;
                if (f->mod[0] == 0u) for (i = zero; i < ccfl / 2u; i++) st->overlap[i] = 0.0f;
                else for (i = ccfl / 2u - lfac; i < ccfl; i++) st->overlap[i] = 0.0f;
            }
            for (i = 0; i < ccfl; i++) { out[i] = st->overlap[i] + fs[i]; st->overlap[i] = 0.0f; }
            st->prev_lpd = 1;
        }
    }
    {   /* a damaged frame can drive the filters to overflow: nothing non-finite may reach the output or stay in the state */
        const float *chk[4] = { out, st->xcit_prev, st->synth_prev, st->exc_prev };
        const unsigned cnt[4] = { ccfl, EXC_PAD, MAX_PITCH + SAUTI_LPD_SYNTH_DELAY_MAX, 257u };
        unsigned q, n;
        for (q = 0; q < 4u; q++) for (n = 0; n < cnt[q]; n++) if (!isfinite(chk[q][n])) return 0;

    }
    return 1;
}
