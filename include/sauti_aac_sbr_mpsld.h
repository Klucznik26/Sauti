#ifndef SAUTI_AAC_SBR_MPSLD_H
#define SAUTI_AAC_SBR_MPSLD_H
/* The MPS low delay QMF (MPSLDFB) that AAC-ELD v2 (ELD with the low delay MPEG Surround) uses in the SBR in place of the CLDFB: non-symmetric prototypes of 320 (32 bands) and 640 (64 bands) coefficients, modulation
 * without the phase rotation of the CLDFB.  Structure from libFDK (qmf.cpp, qmf_pcm.h), checked by the perfect reconstruction of analysis + synthesis (73 dB at 32 -> 32 and 64 -> 64 bands).
 *
 * Analysis (L = 32), x[0..319] the last 320 input samples, x[319] the newest:  u[2L - 1 - k] = 2 SUM_{p = 0..4} c[5k + p] x[k + 2L p]  (k = 0..2L-1);
 *   r[i] = u[i] / 2 - u[2L - 1 - i] / 2,  m[i] = u[i] / 2 + u[2L - 1 - i] / 2;  R = DCT-IV(r), M = DST-IV(m);  X[i] = (-M[i], R[i]) for even i, (M[i], -R[i]) for odd i; times 2 (the level of the other banks).
 * Synthesis (L = 32 or 64): t = DCT-IV(Re X), s = DST-IV(Im X); for i < L / 2: a = -t[i], b = -s[L - 1 - i], c = -t[L - 1 - i], d = -s[i]:
 *   Re[i] = (a + d) ...  see the code; then a 10 tap polyphase filter per output sample (qmfSynPrototypeFirSlot_NonSymmetric) with the first / second half of the prototype. */
#include "sauti_aac_sbr_qmf_analysis.h"

typedef struct { float sta[9 * 64]; uint32_t ready; } SautiAacSbrMpsldSynthState;

int sauti_aac_sbr_qmf_analyze_mpsld(const float *pcm, unsigned slots, SautiAacSbrComplex *out, SautiAacSbrQmfAnalysisState *state);
/* L = 32 (single rate) or 64 (dual rate) bands; grid [slots][stride >= L]; pcm [slots][L] */
int sauti_aac_sbr_qmf_synth_mpsld(unsigned bands, const SautiAacSbrComplex *grid, unsigned stride, unsigned slots, float *pcm, SautiAacSbrMpsldSynthState *state);
#endif
