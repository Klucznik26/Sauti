#ifndef SAUTI_AAC_PS_QMF_LOW_H
#define SAUTI_AAC_PS_QMF_LOW_H
#include "sauti_aac_ps_hybrid.h"
#include "sauti_aac_sbr_hf.h"
#include <stddef.h>

/* PS-1b: 13-tap hybrid analysis of the low QMF bands.
 * Input qmf contains 38 chronological slots of 64 complex bands.
 * Only 32 output slots are produced. History stores the SIX samples
 * immediately preceding the current 32-slot frame for each of 5 bands.
 * The first frame starts with zero history. Only qmf slots [0..37] are
 * inspected; history advances using slots [26..31], not lookahead [32..37].
 * 20-band mode -> 10 low hybrid bins: QMF0=6, QMF1=2, QMF2=2.
 * 34-band mode -> 32 low hybrid bins: 12+8+4+4+4.
 *
 * Each output slot has 10 or 32 contiguous complex samples. The output
 * and history are committed only after successful validation. Input and
 * output must NOT alias (use a separate frame buffer).
 * This stage cannot yet reconstruct HE-AAC v2 stereo PCM. */
#define SAUTI_AAC_PS_LOW_QMF_INPUT  (38u*64u)
#define SAUTI_AAC_PS_LOW_QMF_MAX_BANDS 32u
typedef struct {
    SautiAacSbrComplex history[5u][6u];
    unsigned initialized;
} SautiAacPsQmfLowState;

int sauti_aac_ps_qmf_low_analyze(const SautiAacPsHybridSchedule *schedule,
                                 const SautiAacSbrComplex *qmf,size_t qmf_count,
                                 SautiAacPsQmfLowState *state,
                                 SautiAacSbrComplex *low,size_t low_count);
#endif
