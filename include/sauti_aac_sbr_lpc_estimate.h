#ifndef SAUTI_AAC_SBR_LPC_ESTIMATE_H
#define SAUTI_AAC_SBR_LPC_ESTIMATE_H

#include "sauti_aac_sbr_hf_predict.h"

/* SBR-54g: native complex second-order QMF autoregression.
 *
 * Estimate the two coefficients consumed by SBR-13, for each of the
 * 32 source subbands, from [2 PRECEDING slots + current slots][32].
 *
 * For each source subband, solve least squares for the coefficients
 * of x[t] + alpha0*x[t-1] + alpha1*x[t-2] ~= 0.
 * The estimator is deterministic, regularized and atomic.
 *
 * This is an experimental spectral whitening estimate, not yet the
 * verified ISO 14496-3 reference correlation/estimator. In particular
 * the correct QMF history, noise-band chirp and timing still have to
 * be integrated for end-to-end conformant HE-AAC playback.
 */
int sauti_aac_sbr_lpc_estimate_2(
    const SautiAacSbrComplex *low,size_t low_count,
    unsigned slots,SautiAacSbrLpcCoeffs *out);

#endif
