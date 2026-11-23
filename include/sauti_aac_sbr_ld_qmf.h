#ifndef SAUTI_AAC_SBR_LD_QMF_H
#define SAUTI_AAC_SBR_LD_QMF_H
#include "sauti_aac_sbr_qmf_analysis.h"
#include "sauti_aac_sbr_ld_table.h"

/* The LOW-DELAY complex QMF (CLDFB) of AAC-LD / AAC-ELD with the low-delay SBR tool, 32 bands (single rate: the SBR output has the rate of the core).  Not derived from a published formula: found
 * by identifying the filter banks of libxaac (integer, fixed point: instrumented build, least squares over the 16-bit PCM and the QMF values) and of FDK (structure of libFDK qmf.cpp), see
 * docs/AAC_FAMILY_ASM.md SBR-131.  Prototype: sauti_aac_sbr_ld_qmf_window (320 coefficients, window[i] = value / 64 in the decoder units).
 *
 * Analysis, per slot (32 new samples x[0..31], hist[i] = the sample i steps before the newest one, 320 of them):
 *   u[n] = SUM_{j=0..4} hist[n + 64 j] w[n + 64 j]                              n = 0..63
 *   X[k] = SUM_n u[n] exp(i (pi/64 (2k + 1) n + pi/64 (33 k - 47.5)))           k = 0..31    (the rotation by (33 k - 47.5) pi / 64 includes the 45 degree pattern of the reference)
 * Synthesis, per slot (X[k] complex, zero above the upper SBR border):
 *   Z[k] = X[k] exp(-i alpha_k), alpha = {45, 135, -135, -45} degrees for k mod 4 = 0..3
 *   C = DCT-IV(Re Z), S = DST-IV(Im Z)  (32 points: C[n] = SUM_k Re Z[k] cos(pi/32 (n + 1/2)(k + 1/2)), S likewise with sin)
 *   vr[n] = (C[n] - S[n]) / 2,   vi[n] = -(C[31 - n] + S[31 - n]) / 2
 *   y[32 t + j] = g SUM_{L = 0..8} w'[32 L + j] (L even ? vr_{t - L}[j] : vi_{t - L}[j])        (w' = the window value itself)
 * `analysis_gain` / `synthesis_gain` are chosen so that analysis followed by synthesis is the identity (delay 0 in slots? see the test), the absolute level of the QMF domain is that of the reference. */
#define SAUTI_AAC_SBR_LD_QMF_SLOTS 40u

typedef struct {
    uint32_t ready;
    float vr[9][64], vi[9][64];                    /* the last nine slots of the real and imaginary inverse modulation (newest first; 32 or 64 values per slot) */
} SautiAacSbrLdSynthState;

int sauti_aac_sbr_qmf_analyze_ld(const float *pcm, unsigned slots, SautiAacSbrComplex *out, SautiAacSbrQmfAnalysisState *state);
/* grid: [slots][stride] complex, bands 0..31 used; pcm: [slots][32] */
int sauti_aac_sbr_qmf_synth_ld(const SautiAacSbrComplex *grid, unsigned stride, unsigned slots, float *pcm, SautiAacSbrLdSynthState *state);
/* the same with 64 bands and the 640-coefficient prototype (the DUAL rate: the core runs at half the output rate): grid [slots][stride >= 64], pcm [slots][64] */
int sauti_aac_sbr_qmf_synth_ld64(const SautiAacSbrComplex *grid, unsigned stride, unsigned slots, float *pcm, SautiAacSbrLdSynthState *state);
#endif
