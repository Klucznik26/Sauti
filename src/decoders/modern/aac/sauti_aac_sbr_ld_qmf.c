/* Low-delay complex QMF (CLDFB), 32 bands: see include/sauti_aac_sbr_ld_qmf.h. */
#include "sauti_aac_sbr_ld_qmf.h"
#include <float.h>
#include <math.h>
#include <string.h>

#define PI 3.14159265358979323846264338327950288
/* gains: the analysis output X = ANALYSIS_GAIN * (the sum of the header); the synthesis y = SYNTHESIS_GAIN * (the sum).  Product fixed by the identity test, ANALYSIS_GAIN by the level of the reference. */
#define ANALYSIS_GAIN (1.0 / 8192.0)   /* 2^-13: the level of the classic QMF for a tone (ratio measured 1.2158e-4) */
#define SYNTHESIS_GAIN (SAUTI_AAC_SBR_LD_SYNTH_GAIN_VALUE)
#ifndef SAUTI_AAC_SBR_LD_SYNTH_GAIN64_VALUE
#define SAUTI_AAC_SBR_LD_SYNTH_GAIN64_VALUE (1.0 / 524288.0)
#endif
#define SYNTHESIS_GAIN64 (SAUTI_AAC_SBR_LD_SYNTH_GAIN64_VALUE)
#ifndef SAUTI_AAC_SBR_LD_SYNTH_GAIN_VALUE
#define SAUTI_AAC_SBR_LD_SYNTH_GAIN_VALUE (1.0 / 524288.0)   /* 2^-19 = 2^-32 / ANALYSIS_GAIN: analysis followed by synthesis is the identity (QMF reconstruction 77 dB) */
#endif

int sauti_aac_sbr_qmf_analyze_ld(const float *pcm, unsigned slots, SautiAacSbrComplex *out, SautiAacSbrQmfAnalysisState *state) {
    if (!pcm || !out || !state || slots < 1u || slots > 64u || state->ready > 1u) return 0;
    static double cs[32][64], sn[32][64];
    static int init;
    if (!init) {
        for (unsigned k = 0; k < 32u; k++)
            for (unsigned n = 0; n < 64u; n++) {
                const double th = (PI / 64.0) * (double)(2u * k + 1u) * (double)n + (PI / 64.0) * (33.0 * (double)k - 47.5);
                cs[k][n] = cos(th); sn[k][n] = sin(th);
            }
        init = 1;
    }
    SautiAacSbrQmfAnalysisState next = *state;
    static SautiAacSbrComplex staged[64u * 32u];
    for (unsigned t = 0; t < slots; t++) {
        const float *blk = pcm + (size_t)t * 32u;
        memmove(next.history + 32u, next.history, (320u - 32u) * sizeof(float));
        for (unsigned i = 0; i < 32u; i++) { if (!isfinite(blk[i])) return 0; next.history[31u - i] = blk[i]; }
        next.ready = 1u;
        double u[64];
        for (unsigned n = 0; n < 64u; n++) {
            double s = 0.0;
            for (unsigned j = 0; j < 5u; j++) { const unsigned i = n + 64u * j; s += (double)next.history[i] * (double)sauti_aac_sbr_ld_qmf_window[i]; }
            u[n] = s * ANALYSIS_GAIN;
        }
        for (unsigned k = 0; k < 32u; k++) {
            double re = 0.0, im = 0.0;
            for (unsigned n = 0; n < 64u; n++) { re += u[n] * cs[k][n]; im += u[n] * sn[k][n]; }
            if (!isfinite(re) || !isfinite(im) || fabs(re) > FLT_MAX || fabs(im) > FLT_MAX) return 0;
            staged[(size_t)t * 32u + k].re = (float)re; staged[(size_t)t * 32u + k].im = (float)im;
        }
    }
    next.samples_consumed += (uint64_t)slots * 32u;
    memcpy(out, staged, (size_t)slots * 32u * sizeof(staged[0]));
    *state = next;
    return 1;
}

/* synthesis with N = 32 (single rate, window of 320) or 64 (dual rate, window of 640) bands; `gain` is the synthesis gain of the bank */
static int synth_ld_n(unsigned N, const int16_t *window, double gain, const SautiAacSbrComplex *grid, unsigned stride, unsigned slots, float *pcm, SautiAacSbrLdSynthState *state) {
    if (!grid || !pcm || !state || stride < N || slots < 1u || slots > 64u || state->ready > 1u) return 0;
    static double cosm[2][64][64], sinm[2][64][64], rc[64], rs[64];
    static int init;
    const unsigned sel = N == 64u;
    if (!init) {
        for (unsigned s = 0; s < 2u; s++) {
            const unsigned n_bands = s ? 64u : 32u;
            for (unsigned n = 0; n < n_bands; n++)
                for (unsigned k = 0; k < n_bands; k++) {
                    const double a = (PI / (double)n_bands) * ((double)n + 0.5) * ((double)k + 0.5);
                    cosm[s][n][k] = cos(a); sinm[s][n][k] = sin(a);
                }
        }
        static const double alpha[4] = { PI / 4.0, 3.0 * PI / 4.0, -3.0 * PI / 4.0, -PI / 4.0 };
        for (unsigned k = 0; k < 64u; k++) { rc[k] = cos(alpha[k & 3u]); rs[k] = -sin(alpha[k & 3u]); }   /* exp(-i alpha) = cos(alpha) - i sin(alpha) */
        init = 1;
    }
    SautiAacSbrLdSynthState next = *state;
    static float out[64u * 64u];
    for (unsigned t = 0; t < slots; t++) {
        double zr[64], zi[64], C[64], S[64];
        const SautiAacSbrComplex *x = grid + (size_t)t * stride;
        for (unsigned k = 0; k < N; k++) {
            if (!isfinite(x[k].re) || !isfinite(x[k].im)) return 0;
            zr[k] = (double)x[k].re * rc[k] - (double)x[k].im * rs[k];        /* (re + i im)(c + i s) with s = -sin(alpha) */
            zi[k] = (double)x[k].re * rs[k] + (double)x[k].im * rc[k];
        }
        for (unsigned n = 0; n < N; n++) {
            double c = 0.0, s = 0.0;
            for (unsigned k = 0; k < N; k++) { c += zr[k] * cosm[sel][n][k]; s += zi[k] * sinm[sel][n][k]; }
            C[n] = c; S[n] = s;
        }
        memmove(&next.vr[1][0], &next.vr[0][0], 8u * 64u * sizeof(float));
        memmove(&next.vi[1][0], &next.vi[0][0], 8u * 64u * sizeof(float));
        for (unsigned n = 0; n < N; n++) {
            next.vr[0][n] = (float)((C[n] - S[n]) * 0.5);
            next.vi[0][n] = (float)(-(C[N - 1u - n] + S[N - 1u - n]) * 0.5);
        }
        next.ready = 1u;
        for (unsigned j = 0; j < N; j++) {
            double y = 0.0;
            for (unsigned L = 0; L < 9u; L++) {
                const double w = (double)window[N * L + j];
                y += w * (double)((L & 1u) ? next.vi[L][j] : next.vr[L][j]);
            }
            y *= gain;
            if (!isfinite(y)) return 0;
            out[(size_t)t * N + j] = (float)y;
        }
    }
    memcpy(pcm, out, (size_t)slots * N * sizeof(float));
    *state = next;
    return 1;
}
int sauti_aac_sbr_qmf_synth_ld(const SautiAacSbrComplex *grid, unsigned stride, unsigned slots, float *pcm, SautiAacSbrLdSynthState *state) {
    return synth_ld_n(32u, sauti_aac_sbr_ld_qmf_window, SYNTHESIS_GAIN, grid, stride, slots, pcm, state);
}
int sauti_aac_sbr_qmf_synth_ld64(const SautiAacSbrComplex *grid, unsigned stride, unsigned slots, float *pcm, SautiAacSbrLdSynthState *state) {
    return synth_ld_n(64u, sauti_aac_sbr_ld_qmf_window64, SYNTHESIS_GAIN64, grid, stride, slots, pcm, state);
}
