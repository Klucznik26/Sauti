#ifndef SAUTI_AAC_SBR_TEMPORAL_QMF_H
#define SAUTI_AAC_SBR_TEMPORAL_QMF_H

#include "sauti_aac_sbr_payload.h"
#include "sauti_aac_sbr_qmf_analysis.h"

/* HE-AAC SBR-53b: actual 32-band QMF analysis, followed by the
 * baseline patch transposition selected PER SLOT from SBR-52/53a.
 * Source geometry comes from the original previous/current SBR
 * payload. The analysis filter history advances over all 32 slots
 * as one uninterrupted 1024-sample AAC-LC frame.
 *
 * This is a PREPARATION stage, not decoded HE-AAC PCM: gain
 * limiting, decoded noise/harmonics, LPC/chirp, time alignment
 * and the 64-band synthesis filter are NOT applied.
 *
 * The caller must stage the analysis state separately from the
 * publicly committed playback state until all later steps succeed.
 * On any error no state, low_qmf or baseline_hf is modified.
 * Buffers/state/history must be distinct objects; low and high
 * output arrays must not overlap. No external codec or heap.
 */
int sauti_aac_sbr_temporal_qmf_prepare(
    const SautiAacSbrTemporalHistory *history,
    const float *core_pcm,size_t core_count,
    SautiAacSbrQmfAnalysisState *analysis_state,
    SautiAacSbrComplex *low_qmf,size_t low_count,
    SautiAacSbrComplex *baseline_hf,size_t high_count);

#endif
