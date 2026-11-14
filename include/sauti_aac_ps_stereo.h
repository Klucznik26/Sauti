#ifndef SAUTI_AAC_PS_STEREO_H
#define SAUTI_AAC_PS_STEREO_H

#include "sauti_aac_ps_qmf_bank.h"
#include "sauti_aac_ps_decorrelator.h"
#include "sauti_aac_ps_mix.h"

/* PS-3a: one frame of hybrid-domain stereo. Produces time-major
 * [32][71 or 91] left/right complex HYBRID bands, NOT QMF or PCM.
 * The QMF input must contain 38 time-major slots of 64 complex
 * samples with actual six-slot forward lookahead for low-band FIR.
 * The immutable per-frame schedule/matrix come from the existing
 * render-plan builder. All data, state and output buffers must be
 * distinct. Neither output nor state changes on failure. */
typedef struct {
    SautiAacPsQmfLowState qmf_low;
    SautiAacPsDecorrelatorState decorrelator;
} SautiAacPsStereoState;

int sauti_aac_ps_stereo_hybrid_apply(
    const SautiAacPsHybridSchedule *schedule,
    const SautiAacPsComplexMatrix *matrix,
    const SautiAacSbrComplex *qmf,size_t qmf_count,
    SautiAacPsStereoState *state,
    SautiAacSbrComplex *left,size_t left_count,
    SautiAacSbrComplex *right,size_t right_count);
#endif
