#ifndef SAUTI_AAC_SBR_HF_PREDICT_H
#define SAUTI_AAC_SBR_HF_PREDICT_H

#include "sauti_aac_sbr_hf.h"
#include <stddef.h>
#include <stdint.h>

/* SBR-13: per-noise-band inverse-filtering chirp and the complex
 * second-order LPC prediction FORMULA. Does not estimate LPC coefficients.
 * QMF analysis/synthesis, HF envelope/noise adjustment and PCM remain
 * outside this module. */

typedef struct {
    uint32_t ready;   /* zero-initialize before first frame */
    uint32_t count;   /* 1..5 when ready */
    uint8_t previous_mode[SAUTI_AAC_SBR_MAX_NOISE_BANDS];
    float bw[SAUTI_AAC_SBR_MAX_NOISE_BANDS];
} SautiAacSbrChirp;

typedef struct {
    SautiAacSbrComplex alpha0[SAUTI_AAC_SBR_HF_LOW_BANDS];
    SautiAacSbrComplex alpha1[SAUTI_AAC_SBR_HF_LOW_BANDS];
} SautiAacSbrLpcCoeffs;

/* Given SBR-06 inverse filter modes, update smoothed bandwidth factors
 * and mode history. Validates all active values and requires identical
 * band geometry after initialization. To change geometry, reset chirp
 * state to zero first. On failure *state remains unchanged. */
int sauti_aac_sbr_chirp_update(const uint8_t *invf_mode,
                               unsigned noise_band_count,
                               SautiAacSbrChirp *state);

/* Complex QMF high-band generation from 2 history rows plus 'slots'
 * current rows, laid out time-major as low[slots+2][32].
 * For every mapped high bin k with source p and noise band g:
 * high[t][k] = low[t+2][p] +
 *      bw[g]*alpha0[p]*low[t+1][p] +
 *      bw[g]^2*alpha1[p]*low[t][p].
 * All three multiplications are COMPLEX.
 *
 * Coefficients are supplied by the caller for now (not estimated by this
 * module). All unused high bins remain zero. This operation needs
 * pre-analyzed complex QMF samples; it is NOT a QMF filter bank.
 * Handles overlapping buffers through local staging. No malloc.
 * Returns zero without modifying high[] on any malformed map,
 * nonfinite utilized value, invalid filter, or short buffer. */
int sauti_aac_sbr_hf_predict(const SautiAacSbrPatches *patches,
                              const SautiAacSbrTables *tables,
                              const SautiAacSbrComplex *low,
                              size_t low_count,
                              unsigned slots,
                              const SautiAacSbrLpcCoeffs *coeffs,
                              const SautiAacSbrChirp *chirp,
                              SautiAacSbrComplex *high,
                              size_t high_count);

#endif
