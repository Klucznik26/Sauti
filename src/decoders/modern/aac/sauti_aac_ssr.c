/* AAC SSR (object type 3): gain control tool, per-band IMDCT and the inverse polyphase quadrature filter bank, see include/sauti_aac_ssr.h.  Written for Sauti; the structure of the tool
 * (band-wise IMDCT, gain modification function from the gain change points, overlap-add per band, 4-band IPQF) follows the description in the ISO/IEC 14496-5 reference software, whose licence
 * notice is in third_party/iso14496-5/NOTICE (the 48 PQF prototype coefficients in sauti_aac_ssr_tables.c are data from it). */
#include "sauti_aac_ssr.h"
#include <math.h>
#include <stdatomic.h>
#include <string.h>

#define NB SAUTI_AAC_SSR_BANDS
#define PI 3.14159265358979323846264338327950288
#define OUT_SCALE 2.0                    /* the AAC-LC synthesis of Sauti is 2 / lines (ISO: 1 / lines): the same convention for the band IMDCT */

/* ---------------------------------------------------------------------- tables computed once */
static double win_sin_long[256], win_sin_short[32], win_kbd_long[256], win_kbd_short[32];
static double cos_long[2048], cos_short[256];                          /* cos(pi j / (4 nf)), j < 8 nf, for nf = 256 and nf = 32 */
static double pqf_q0[2][4], pqf_q1[2][4], pqf_t0[4][12], pqf_t1[4][12];
static atomic_int tables_state;                                        /* 0 = not built, 1 = being built, 2 = ready */

static double bessel_i0(double x) {
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 80; k++) { term *= (x / (2.0 * k)) * (x / (2.0 * k)); sum += term; if (term < sum * 1e-18) break; }
    return sum;
}
static void kbd(double *w, unsigned n, double alpha) {                  /* Kaiser-Bessel-derived window, first half of 2n points */
    double k[257], total = 0.0, run = 0.0;
    for (unsigned j = 0; j <= n; j++) { const double t = 2.0 * (double)j / (double)n - 1.0; k[j] = bessel_i0(PI * alpha * sqrt(1.0 - t * t)); total += k[j]; }
    for (unsigned j = 0; j < n; j++) { run += k[j]; w[j] = sqrt(run / total); }
}
static void tables_build(void) {
    for (unsigned i = 0; i < 256; i++) win_sin_long[i] = sin(PI / 512.0 * ((double)i + 0.5));
    for (unsigned i = 0; i < 32; i++) win_sin_short[i] = sin(PI / 64.0 * ((double)i + 0.5));
    kbd(win_kbd_long, 256, 4.0);
    kbd(win_kbd_short, 32, 6.0);
    for (unsigned j = 0; j < 2048; j++) cos_long[j] = cos(PI * (double)j / 1024.0);
    for (unsigned j = 0; j < 256; j++) cos_short[j] = cos(PI * (double)j / 128.0);
    double proto[96];
    for (unsigned j = 0; j < 48; j++) proto[j] = proto[95 - j] = sauti_aac_ssr_pqf_half[j];
    for (unsigned n = 0; n < 2; n++)
        for (unsigned i = 0; i < 4; i++) {
            pqf_q0[n][i] = 2.0 * cos((double)((2 * i + 1) * (2 * n + 1 - 4)) * PI / 16.0);
            pqf_q1[n][i] = 2.0 * cos((double)((2 * i + 1) * (2 * (4 + n) + 1 - 4)) * PI / 16.0);
        }
    for (unsigned n = 0; n < 4; n++)
        for (unsigned k = 0; k < 12; k++) {
            const double sign = (k & 1u) ? -1.0 : 1.0;
            pqf_t0[n][k] = sign * 4.0 * proto[2 * k * 4 + n];
            pqf_t1[n][k] = sign * 4.0 * proto[(2 * k + 1) * 4 + n];
        }
}
static void tables_ready(void) {
    int expected = 0;
    if (atomic_load_explicit(&tables_state, memory_order_acquire) == 2) return;
    if (atomic_compare_exchange_strong(&tables_state, &expected, 1)) { tables_build(); atomic_store_explicit(&tables_state, 2, memory_order_release); return; }
    while (atomic_load_explicit(&tables_state, memory_order_acquire) != 2) { }
}

void sauti_aac_ssr_state_init(SautiAacSsrState *state) { memset(state, 0, sizeof *state); tables_ready(); }

/* ---------------------------------------------------------------------- gain_control_data() */
static int get(const uint8_t *data, size_t length, size_t *pos, unsigned width, unsigned *value) {      /* MSB-first bits; pos counts bits */
    uint32_t v = 0;
    if (width > 24u || *pos > length * 8u || length * 8u - *pos < width) return 0;
    for (unsigned i = 0; i < width; i++, (*pos)++) v = (v << 1) | (unsigned)((data[*pos >> 3] >> (7u - (*pos & 7u))) & 1u);
    *value = v;
    return 1;
}
int sauti_aac_ssr_gain_parse(const uint8_t *data, size_t length, size_t *pos, unsigned window_sequence, SautiAacSsrGain *gain) {
    unsigned blocks, loc_first, loc_other, v;
    if (!data || !pos || !gain || window_sequence > 3u) return 0;
    switch (window_sequence) {
    case 0u: blocks = 1; loc_first = loc_other = 5; break;                 /* long: 5-bit locations */
    case 2u: blocks = 8; loc_first = loc_other = 2; break;                 /* eight short windows: 2-bit locations */
    case 1u: blocks = 2; loc_first = 4; loc_other = 2; break;              /* long start: 4 bits in the first block, 2 in the second */
    default: blocks = 2; loc_first = 4; loc_other = 5; break;              /* long stop: 4 bits in the first block, 5 in the second */
    }
    SautiAacSsrGain next;
    memset(&next, 0, sizeof next);
    if (!get(data, length, pos, 2, &v)) return 0;
    next.max_band = (uint8_t)v;
    for (unsigned band = 1; band <= v; band++)
        for (unsigned blk = 0; blk < blocks; blk++) {
            SautiAacSsrGainInf *g = &next.band[band][blk];
            unsigned n;
            if (!get(data, length, pos, 3, &n)) return 0;
            g->num = (uint8_t)n;
            for (unsigned k = 0; k < n; k++) {
                unsigned lev, loc;
                if (!get(data, length, pos, 4, &lev) || !get(data, length, pos, blk == 0 ? loc_first : loc_other, &loc)) return 0;
                g->lev[k] = (uint8_t)lev; g->loc[k] = (uint8_t)loc;
            }
        }
    *gain = next;
    return 1;
}

/* ---------------------------------------------------------------------- gain control function */
typedef struct { int aloc[10]; double alev[10]; } GainWork;
/* The modification function of one half-window region from the gain change points: between two points the gain is interpolated in the log domain over 8 samples.  Returns the level of the first
 * point (1 without data). */
static double fragment(const SautiAacSsrGainInf *g, int max_loc, GainWork *w, double *out) {
    const int num = g ? (int)g->num : 0;
    for (int i = 0; i < num; i++) { w->aloc[i + 1] = 8 * (int)g->loc[i]; w->alev[i + 1] = ldexp(1.0, (int)g->lev[i] - 4); }
    w->aloc[0] = 0;
    w->alev[0] = num == 0 ? 1.0 : w->alev[1];
    const double first = w->alev[0];
    w->aloc[num + 1] = max_loc; w->alev[num + 1] = 1.0;
    for (int i = 0; i < max_loc; i++) {
        int m = 0;
        for (int j = 0; j <= num + 1; j++) if (w->aloc[j] <= i) m = j;
        if (i >= w->aloc[m] && i <= w->aloc[m] + 7) {
            const int d = i - w->aloc[m];
            out[i] = exp2(((double)(8 - d) * (double)ilogb(w->alev[m]) + (double)d * (double)ilogb(w->alev[m + 1])) / 8.0);
        } else out[i] = w->alev[m + 1];
    }
    return first;
}
/* The gain control function (the reciprocal of the modification function) of one window of `ns` samples: the previous block's data act on the first half, the current block's on the second. */
static void gain_window(unsigned ns, const SautiAacSsrGainInf *prev, const SautiAacSsrGainInf *blocks, unsigned type, unsigned id, double *ad) {
    double mod[512], f0[256], f1[256], f2[256];
    GainWork w;
    int m0, m1, m2, flat = 0;
    memset(&w, 0, sizeof w);
    switch (type) {
    case 0u: case 2u: m0 = m1 = (int)ns / 2; m2 = 0; break;
    case 1u: m0 = (int)ns / 2; m1 = (int)(ns * 7u / 32u); m2 = (int)ns / 16; break;
    default: m0 = (int)ns / 16; m1 = (int)(ns * 7u / 32u); m2 = (int)ns / 2; break;
    }
    (void)fragment(prev, m0, &w, f0);
    const double second = fragment(&blocks[type == 2u ? id : 0u], m1, &w, f1);
    if (type == 1u || type == 3u) (void)fragment(&blocks[1], m2, &w, f2);
    if (type == 3u) { flat = (int)ns / 2 - m0 - m1; for (int i = 0; i < flat; i++) mod[i] = 1.0; }
    if (type == 0u || type == 2u) w.alev[0] = 1.0;
    for (int i = 0; i < m0; i++) mod[i + flat] = w.alev[0] * second * f0[i];
    for (int i = 0; i < m1; i++) mod[i + flat + m0] = w.alev[0] * f1[i];
    if (type == 1u) {
        for (int i = 0; i < m2; i++) mod[i + m0 + m1] = f2[i];
        flat = (int)ns / 2 - m1 - m2;
        for (int i = 0; i < flat; i++) mod[i + m0 + m1 + m2] = 1.0;
    } else if (type == 3u) {
        for (int i = 0; i < m2; i++) mod[i + flat + m0 + m1] = f2[i];
    }
    for (unsigned i = 0; i < ns; i++) ad[i] = 1.0 / mod[i];
}

/* ---------------------------------------------------------------------- IMDCT of one band */
/* x[n] = 1/nf * sum_k X[k] cos(pi (2n + nf + 1)(2k + 1) / (4 nf)), n < 2 nf (the ISO scale; the caller applies OUT_SCALE) */
static void imdct(const double *X, unsigned nf, const double *ctab, double *x) {
    const unsigned period = 8u * nf;
    for (unsigned n = 0; n < 2u * nf; n++) {
        double s = 0.0;
        unsigned m = (2u * n + nf + 1u) % period;                        /* (2n + nf + 1)(2k + 1) mod 8 nf, stepping k by 2 * m */
        const unsigned step = (2u * m) % period;
        unsigned idx = m;
        for (unsigned k = 0; k < nf; k++) { s += X[k] * ctab[idx]; idx += step; if (idx >= period) idx -= period; }
        x[n] = s / (double)nf;
    }
}

/* ---------------------------------------------------------------------- one frame */
int sauti_aac_ssr_synthesis(SautiAacSsrState *st, unsigned ws, unsigned prev_shape, unsigned shape,
                            const float spectral[1024], const SautiAacSsrGain *gain, float out[1024]) {
    static const SautiAacSsrGainInf none[8];
    double in[NB][256], tb[512], gw[512], band_out[NB][256], w[512], w1[64], w27[64];
    const double *sl_prev = prev_shape ? win_kbd_long : win_sin_long, *sl_cur = shape ? win_kbd_long : win_sin_long;
    const double *ss_prev = prev_shape ? win_kbd_short : win_sin_short, *ss_cur = shape ? win_kbd_short : win_sin_short;
    if (!st || !spectral || !out || ws > 3u || prev_shape > 1u || shape > 1u) return 0;
    tables_ready();
    /* spectral lines -> band order (odd bands reversed) */
    for (unsigned b = 0; b < NB; b++)
        for (unsigned i = 0; i < 256; i++) {
            if (ws == 2u) { const unsigned blk = i / 32u, j = i % 32u; in[b][i] = spectral[128u * blk + 32u * b + ((b & 1u) ? 31u - j : j)]; }
            else in[b][i] = spectral[256u * b + ((b & 1u) ? 255u - i : i)];
        }
    /* IMDCT windows */
    if (ws == 0u) { for (unsigned i = 0; i < 256; i++) { w[i] = sl_prev[i]; w[511 - i] = sl_cur[i]; } }
    else if (ws == 1u) {
        for (unsigned i = 0; i < 256; i++) w[i] = sl_prev[i];
        for (unsigned i = 0; i < 112; i++) w[256 + i] = 1.0;
        for (unsigned i = 0; i < 32; i++) w[368 + i] = ss_cur[31 - i];
        for (unsigned i = 0; i < 112; i++) w[400 + i] = 0.0;
    } else if (ws == 3u) {
        for (unsigned i = 0; i < 112; i++) w[i] = 0.0;
        for (unsigned i = 0; i < 32; i++) w[112 + i] = ss_prev[i];
        for (unsigned i = 0; i < 112; i++) w[144 + i] = 1.0;
        for (unsigned i = 0; i < 256; i++) w[256 + i] = sl_cur[255 - i];
    } else {
        for (unsigned i = 0; i < 32; i++) { w1[i] = ss_prev[i]; w1[63 - i] = ss_cur[i]; w27[i] = w27[63 - i] = ss_cur[i]; }
    }
    for (unsigned b = 0; b < NB; b++) {
        const SautiAacSsrGainInf *blocks = gain ? gain->band[b] : none;
        if (st->bands && st->bands < NB && b >= st->bands) { memset(band_out[b], 0, sizeof band_out[b]); continue; }       /* scalable sampling rate: the band is not decoded */
        /* IMDCT and window */
        if (ws == 2u) {
            for (unsigned blk = 0; blk < 8; blk++) {
                double x[64];
                imdct(in[b] + 32u * blk, 32, cos_short, x);
                for (unsigned i = 0; i < 64; i++) tb[blk * 64u + i] = x[i] * (blk == 0 ? w1[i] : w27[i]);
            }
        } else {
            imdct(in[b], 256, cos_long, tb);
            for (unsigned i = 0; i < 512; i++) tb[i] *= w[i];
        }
        /* gain compensation and overlap-add */
        if (ws == 2u) {
            for (unsigned k = 0; k < 8; k++) {
                gain_window(64, &st->prev[b], blocks, 2u, k, gw);
                for (unsigned j = 0; j < 64; j++) tb[k * 64u + j] *= gw[j];
                for (unsigned j = 0; j < 32; j++) st->overlap[b][112u + 32u * k + j] += tb[k * 64u + j];
                for (unsigned j = 0; j < 32; j++) st->overlap[b][112u + 32u * (k + 1u) + j] = tb[k * 64u + 32u + j];
                st->prev[b] = blocks[k];
            }
            for (unsigned j = 0; j < 256; j++) band_out[b][j] = st->overlap[b][j];
            for (unsigned j = 0; j < 256; j++) st->overlap[b][j] = st->overlap[b][j + 256];
        } else {
            gain_window(512, &st->prev[b], blocks, ws, 0, gw);
            for (unsigned j = 0; j < 512; j++) tb[j] *= gw[j];
            for (unsigned j = 0; j < 256; j++) { band_out[b][j] = st->overlap[b][j] + tb[j]; st->overlap[b][j] = tb[256 + j]; }
            st->prev[b] = blocks[ws == 0u ? 0u : 1u];
        }
    }
    /* IPQF: 256 slots of four band samples -> four output samples each */
    for (unsigned s = 0; s < 256; s++) {
        double v[4], r[4];
        for (unsigned j = 0; j < 4; j++) v[j] = band_out[j][s];
        for (unsigned n = 0; n < 2; n++) {
            double a0 = 0.0, a1 = 0.0;
            memmove(st->pqf0[n], st->pqf0[n] + 1, 23 * sizeof(double));
            memmove(st->pqf1[n], st->pqf1[n] + 1, 23 * sizeof(double));
            for (unsigned i = 0; i < 4; i++) { a0 += pqf_q0[n][i] * v[i]; a1 += pqf_q1[n][i] * v[i]; }
            st->pqf0[n][23] = a0; st->pqf1[n][23] = a1;
        }
        for (unsigned n = 0; n < 2; n++) {
            double lo = 0.0, hi = 0.0;
            for (unsigned k = 0; k < 12; k++) {
                const double b0 = st->pqf0[n][23 - 2 * k], b1 = st->pqf1[n][22 - 2 * k];
                lo += pqf_t0[n][k] * b0 + pqf_t1[n][k] * b1;
                hi += pqf_t0[3 - n][k] * b0 - pqf_t1[3 - n][k] * b1;
            }
            r[n] = lo; r[3 - n] = hi;
        }
        for (unsigned j = 0; j < 4; j++) out[4 * s + j] = (float)(r[j] * OUT_SCALE);
    }
    return 1;
}
