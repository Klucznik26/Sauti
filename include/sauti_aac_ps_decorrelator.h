#ifndef SAUTI_AAC_PS_DECORRELATOR_H
#define SAUTI_AAC_PS_DECORRELATOR_H

#include "sauti_aac_ps_transient.h"
#include "sauti_aac_ps_decorrelate.h"

/* PS-2c: apply the independent 20/34-band transient gains after
 * complex all-pass decorrelation, using the schedule's k->parameter map.
 * Input and output are time-major [32][71 or 91] complex values.
 * Both persistent states and the output are committed as one transaction.
 * All pointers are non-overlapping; input and output cannot alias.
 * Not a stereo QMF synthesis / PCM decoder. */
typedef struct {
    SautiAacPsTransientState transient;
    SautiAacPsDecorrelateState decorrelation;
} SautiAacPsDecorrelatorState;

int sauti_aac_ps_decorrelator_apply(const SautiAacPsHybridSchedule *schedule,
                                    const SautiAacSbrComplex *direct,size_t direct_count,
                                    SautiAacPsDecorrelatorState *state,
                                    SautiAacSbrComplex *out,size_t out_count);
#endif
