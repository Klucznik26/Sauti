#ifndef SAUTI_AAC_PS_QMF_HIGH_H
#define SAUTI_AAC_PS_QMF_HIGH_H

#include "sauti_aac_ps_hybrid.h"
#include "sauti_aac_sbr_hf.h"
#include <stddef.h>

/* PS-1a: UNSPLIT upper-QMF portion of the hybrid analysis bank.
 * 20 parameter bands: hybrid [10,71) corresponds to QMF [3,64).
 * 34 parameter bands: hybrid [32,91) corresponds to QMF [5,64).
 * Low hybrid bands still require the 13-tap analysis filterbank; callers
 * MUST NOT treat the returned high-band samples as a complete PS signal.
 *
 * qmf: 38 time-major slots of [64] complex QMF samples, of which the first 32 are consumed
 * directly, WITHOUT a six-slot shift (PS hybrid analysis reference).
 * high: only the upper bands, [32][61] or [32][59], in time-major order.
 * Counts are complex sample entries. The output prefix is atomic, supports
 * source/output overlap, and never changes on failure.
 * This API is NOT stereo PCM reconstruction. */
#define SAUTI_AAC_PS_HIGH_QMF_INPUT_SLOTS 38u
#define SAUTI_AAC_PS_HIGH_QMF_BANDS 64u
#define SAUTI_AAC_PS_HIGH_QMF_MAX_BANDS 61u

int sauti_aac_ps_qmf_high_extract(const SautiAacPsHybridSchedule *schedule,
                                  const SautiAacSbrComplex *qmf,
                                  size_t qmf_count,
                                  SautiAacSbrComplex *high,
                                  size_t high_count);
#endif
