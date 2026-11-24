/* MPS low delay QMF: see include/sauti_aac_sbr_mpsld.h. */
#include "sauti_aac_sbr_mpsld.h"
#include "sauti_aac_sbr_mpsld_table.h"
#include <float.h>
#include <math.h>
#include <string.h>

#define PI 3.14159265358979323846264338327950288
#define ANALYSIS_GAIN 2.0
#define SYNTHESIS_GAIN (1.0 / 32.0)

static double tcos[2][64][64], tsin[2][64][64];
static int tinit;
static void init_tables(void) {
    if (tinit) return;
    for (unsigned s = 0; s < 2u; s++) {
        const unsigned L = s ? 64u : 32u;
        for (unsigned n = 0; n < L; n++) for (unsigned k = 0; k < L; k++) { const double a = PI / (double)L * ((double)n + 0.5) * ((double)k + 0.5); tcos[s][n][k] = cos(a); tsin[s][n][k] = sin(a); }
    }
    tinit = 1;
}

int sauti_aac_sbr_qmf_analyze_mpsld(const float *pcm, unsigned slots, SautiAacSbrComplex *out, SautiAacSbrQmfAnalysisState *state) {
    static SautiAacSbrComplex staged[64u * 32u];
    SautiAacSbrQmfAnalysisState next;
    if (!pcm || !out || !state || slots < 1u || slots > 64u || state->ready > 1u) return 0;
    init_tables();
    next = *state;
    for (unsigned t = 0; t < slots; t++) {
        const float *blk = pcm + (size_t)t * 32u;
        double u[64], r[32], m[32];
        memmove(next.history + 32u, next.history, (320u - 32u) * sizeof(float));
        for (unsigned i = 0; i < 32u; i++) { if (!isfinite(blk[i])) return 0; next.history[31u - i] = blk[i]; }
        next.ready = 1u;
        for (unsigned k = 0; k < 64u; k++) {                                   /* x[i] = history[319 - i] */
            double a = 0.0;
            for (unsigned p = 0; p < 5u; p++) a += (double)sauti_aac_sbr_mpsld_proto320[5u * k + p] * (double)next.history[319u - (k + 64u * p)];
            u[63u - k] = 2.0 * a;
        }
        for (unsigned i = 0; i < 32u; i++) { r[i] = u[i] * 0.5 - u[63u - i] * 0.5; m[i] = u[i] * 0.5 + u[63u - i] * 0.5; }
        /* the folding of the reference pairs (i, i + 1) the same way; the formula above is its closed form for every i */
        for (unsigned k = 0; k < 32u; k++) {
            double R = 0.0, M = 0.0;
            for (unsigned n = 0; n < 32u; n++) { R += r[n] * tcos[0][n][k]; M += m[n] * tsin[0][n][k]; }
            R *= ANALYSIS_GAIN; M *= ANALYSIS_GAIN;
            staged[(size_t)t * 32u + k].re = (float)((k & 1u) ? M : -M);
            staged[(size_t)t * 32u + k].im = (float)((k & 1u) ? -R : R);
        }
    }
    next.samples_consumed += (uint64_t)slots * 32u;
    memcpy(out, staged, (size_t)slots * 32u * sizeof(staged[0]));
    *state = next;
    return 1;
}

int sauti_aac_sbr_qmf_synth_mpsld(unsigned bands, const SautiAacSbrComplex *grid, unsigned stride, unsigned slots, float *pcm, SautiAacSbrMpsldSynthState *state) {
    static float out[64u * 64u];
    SautiAacSbrMpsldSynthState next;
    const unsigned L = bands, sel = L == 64u;
    const float *pf = sel ? sauti_aac_sbr_mpsld_proto640 : sauti_aac_sbr_mpsld_proto320, *pm = pf + (sel ? 320u : 160u);
    if (!grid || !pcm || !state || (L != 32u && L != 64u) || stride < L || slots < 1u || slots > 64u || state->ready > 1u) return 0;
    init_tables();
    next = *state;
    for (unsigned t = 0; t < slots; t++) {
        double tr[64], ti[64], R[64], I[64];
        const SautiAacSbrComplex *x = grid + (size_t)t * stride;
        for (unsigned n = 0; n < L; n++) {
            double c = 0.0, s = 0.0;
            for (unsigned k = 0; k < L; k++) { if (!isfinite(x[k].re) || !isfinite(x[k].im)) return 0; c += (double)x[k].re * tcos[sel][n][k]; s += (double)x[k].im * tsin[sel][n][k]; }
            tr[n] = c; ti[n] = s;
        }
        for (unsigned i = 0; i < L / 2u; i++) {
            const double r1 = -tr[i], i2 = -ti[L - 1u - i], r2 = -tr[L - 1u - i], i1 = -ti[i];
            R[i] = (r1 - i1) * 0.5; I[L - 1u - i] = -(r1 + i1) * 0.5; R[L - 1u - i] = (r2 - i2) * 0.5; I[i] = -(r2 + i2) * 0.5;
        }
        for (int j = (int)L - 1; j >= 0; j--) {
            const unsigned g = L - 1u - (unsigned)j;
            float *sta = next.sta + 9u * g;
            const float *fl = pf + 5u * g, *fm = pm + 5u * g;
            double y = (double)sta[0] + (double)fm[4] * R[j];
            y *= SYNTHESIS_GAIN;
            if (!isfinite(y)) return 0;
            out[(size_t)t * L + (unsigned)j] = (float)y;
            sta[0] = (float)((double)sta[1] + (double)fl[4] * I[j]);
            sta[1] = (float)((double)sta[2] + (double)fm[3] * R[j]);
            sta[2] = (float)((double)sta[3] + (double)fl[3] * I[j]);
            sta[3] = (float)((double)sta[4] + (double)fm[2] * R[j]);
            sta[4] = (float)((double)sta[5] + (double)fl[2] * I[j]);
            sta[5] = (float)((double)sta[6] + (double)fm[1] * R[j]);
            sta[6] = (float)((double)sta[7] + (double)fl[1] * I[j]);
            sta[7] = (float)((double)sta[8] + (double)fm[0] * R[j]);
            sta[8] = (float)((double)fl[0] * I[j]);
        }
        next.ready = 1u;
    }
    memcpy(pcm, out, (size_t)slots * L * sizeof(float));
    *state = next;
    return 1;
}
