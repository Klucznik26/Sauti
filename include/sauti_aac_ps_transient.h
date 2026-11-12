#ifndef SAUTI_AAC_PS_TRANSIENT_H
#define SAUTI_AAC_PS_TRANSIENT_H
#include "sauti_aac_ps_hybrid.h"
#include "sauti_aac_sbr_hf.h"
#include <stddef.h>

/* PS-2a: parameter-band transient attenuation feeding the decorrelator.
 * Input time-major [32][71 or 91] complex hybrid samples.
 * Output time-major [32][20 or 34] finite gains in [0,1].
 * Values are local to one frame, while three running energy statistics
 * persist across frames, keyed by parameter-band topology.
 *
 * On validation/math failure neither output nor state is changed.
 * Input, state and output must be non-overlapping. */
typedef struct {
    float peak[34];
    float smooth[34];
    float diff[34];
    unsigned bands; /* 0 (reset), 20 or 34 */
} SautiAacPsTransientState;

int sauti_aac_ps_transient_gains(const SautiAacPsHybridSchedule *schedule,
                                 const SautiAacSbrComplex *input,size_t input_count,
                                 SautiAacPsTransientState *state,
                                 float *gains,size_t gain_count);
#endif
