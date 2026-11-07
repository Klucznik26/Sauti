#ifndef SAUTI_AAC_PS_PHASE_H
#define SAUTI_AAC_PS_PHASE_H

#include "sauti_aac_ps_matrix.h"

typedef struct { float re,im; } SautiAacPsComplexCoefficient;
typedef struct {
    SautiAacPsComplexCoefficient h11,h12,h21,h22;
} SautiAacPsComplexMatrixCoefficients;
typedef struct {
    uint32_t bands;
    uint32_t env_count;
    uint32_t has_phase;
    SautiAacPsComplexMatrixCoefficients coefficient
        [SAUTI_AAC_PS_MAX_ENVELOPES][SAUTI_AAC_PS_MAX_PARAMETERS];
} SautiAacPsComplexMatrix;

/* Three-symbol IPD/OPD smoothing history.  A 20<->34-band transition resets
 * it, matching PS parameter-band topology. */
typedef struct {
    uint32_t ready;
    uint32_t bands;
    uint8_t ipd_history[SAUTI_AAC_PS_MAX_PHASE_PARAMETERS];
    uint8_t opd_history[SAUTI_AAC_PS_MAX_PHASE_PARAMETERS];
} SautiAacPsPhaseState;

/* Applies phase smoothing and creates complex PS coefficients atomically.
 * Matrix input remains usable for baseline PS with IPD/OPD absent. */
int sauti_aac_ps_phase_apply(const SautiAacPsFrame *frame,
                             const SautiAacPsMappedFrame *mapped,
                             const SautiAacPsMatrix *matrix,
                             SautiAacPsPhaseState *state,
                             SautiAacPsComplexMatrix *out);

#endif
