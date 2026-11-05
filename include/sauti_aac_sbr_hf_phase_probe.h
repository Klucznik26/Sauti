#ifndef SAUTI_AAC_SBR_HF_PHASE_PROBE_H
#define SAUTI_AAC_SBR_HF_PHASE_PROBE_H
#include "sauti_aac_sbr_temporal_noise.h"

/* SBR-54q: diagnostic conjugation of emitted HF QMF, NOT normative SBR.
 * The original 32-band analysis, LOW QMF, patch map, gain, and
 * 64-QMF synthesis stay untouched. On failure, output is unchanged.
 * Check both parts of the complex plane; do not equate negative
 * time-domain correlation with proof of wrong imaginary sign. */
int sauti_aac_sbr_hf_conjugate_probe(
    const SautiAacSbrComplex *high,size_t high_count,
    const SautiAacSbrTemporalNoiseFrame *mask,
    SautiAacSbrComplex *out,size_t out_count);

#endif
