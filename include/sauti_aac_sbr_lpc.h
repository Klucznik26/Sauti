#ifndef SAUTI_AAC_SBR_LPC_H
#define SAUTI_AAC_SBR_LPC_H

#include "sauti_aac_sbr_hf_predict.h"
#include <stddef.h>

/* SBR-14: estimation of native complex second-order LPC coefficients
 * for HE-AAC high-frequency transposition. Exactly 40 consecutive
 * pre-analyzed complex QMF time slots are required, with two history
 * slots and 38 subsequent slots, laid out low[40][32].
 *
 * Each low QMF band is solved independently using the SBR complex
 * lag-0, lag-1 and lag-2 correlations and a regularized determinant.
 * Near-singular fits and norm >= 4 coefficients fall back to ZERO,
 * preventing spectral instability. Zero amplitudes are valid input.
 *
 * 'bands' (1..32) is the number of used low QMF bands; unused output
 * alpha coefficients are zeroed. A nonfinite sample in a used band,
 * invalid argument or short buffer leaves *out unchanged. */
#define SAUTI_AAC_SBR_LPC_WINDOW 40u

int sauti_aac_sbr_lpc_estimate(const SautiAacSbrComplex *low,
                               size_t low_count,
                               unsigned bands,
                               SautiAacSbrLpcCoeffs *out);

#endif
