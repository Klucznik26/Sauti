#ifndef SAUTI_AAC_PS_HYBRID_SYNTH_H
#define SAUTI_AAC_PS_HYBRID_SYNTH_H

#include "sauti_aac_ps_hybrid.h"
#include "sauti_aac_sbr_hf.h"
#include <stddef.h>

/* PS-3b: 32 slots of the inverse hybrid filterbank.
 * Input: time-major [32][71] or [32][91] complex hybrid bands.
 * Output: time-major [32][64] complex QMF bands.
 *
 * PS 20: group 6+2+2 low bands to QMF0..2, copy hybrid10..70.
 * PS 34: group 12+8+4+4+4 to QMF0..4, copy hybrid32..90.
 * There is no output history; the analysis-side FIR owns its history.
 *
 * Input/output must be disjoint. On failure output remains unchanged.
 * This is not SBR synthesis and does not produce stereo PCM. */
int sauti_aac_ps_hybrid_synthesize(const SautiAacPsHybridSchedule *schedule,
                                   const SautiAacSbrComplex *hybrid,size_t hybrid_count,
                                   SautiAacSbrComplex *qmf,size_t qmf_count);

/* One atomic dual-channel operation; left/right hybrid sources and
 * QMF destinations must all be pairwise non-overlapping. */
int sauti_aac_ps_hybrid_synthesize_stereo(const SautiAacPsHybridSchedule *schedule,
                                          const SautiAacSbrComplex *left,size_t left_count,
                                          const SautiAacSbrComplex *right,size_t right_count,
                                          SautiAacSbrComplex *left_qmf,size_t left_qmf_count,
                                          SautiAacSbrComplex *right_qmf,size_t right_qmf_count);
#endif
