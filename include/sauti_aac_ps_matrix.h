#ifndef SAUTI_AAC_PS_MATRIX_H
#define SAUTI_AAC_PS_MATRIX_H

#include "sauti_aac_ps_map.h"

/* Real components of the PS stereo matrix for one parameter band:
 * left = h11 * direct + h21 * decorrelated;
 * right = h12 * direct + h22 * decorrelated.
 * CAUTION: coefficients are indexed by input (direct, decorrelated)
 * and output (left, right), not row-major output-first.
 * IPD/OPD contributes a separate complex rotation in the mixer stage. */
typedef struct {
    float h11;
    float h12;
    float h21;
    float h22;
} SautiAacPsMatrixCoefficients;

typedef struct {
    uint32_t bands;     /* 20 or 34 */
    uint32_t env_count; /* 1..5 */
    uint32_t variant_b; /* ICC mode 3..5, otherwise matrix A */
    SautiAacPsMatrixCoefficients coefficient
        [SAUTI_AAC_PS_MAX_ENVELOPES][SAUTI_AAC_PS_MAX_PARAMETERS];
} SautiAacPsMatrix;

/* Dequantizes IID/ICC and constructs ISO PS matrix A or B for every mapped
 * envelope atomically.  This is cold control work; the complex per-QMF-slot
 * mixer is deliberately separate and may receive an ASM implementation. */
int sauti_aac_ps_matrix_build(const SautiAacPsFrame *frame,
                              const SautiAacPsMappedFrame *mapped,
                              SautiAacPsMatrix *out);

#endif
