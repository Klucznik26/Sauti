#ifndef SAUTI_AAC_SBR_TEMPORAL_SOURCE6_H
#define SAUTI_AAC_SBR_TEMPORAL_SOURCE6_H

#include "sauti_aac_sbr_temporal_qmf.h"

/* SBR-54s: diagnostic-only six-slot lookback source-QMF provenance.
 *
 * SBR-54r modeled the FFmpeg source for synthesis slot i as
 * analysis W[i+ENVELOPE_ADJUSTMENT_OFFSET-t_HFGen] = W[i-6].
 * This is a SOURCE-INDEX experiment, NOT a HE-AAC conformance fix.
 *
 * PREVIOUS SOURCE-QMF samples are independent of the 2/6-slot FIXVAR
 * side-data owner. The HF patch geometry still comes from SBR-52/53a
 * ORIGINAL owner per OUTPUT slot.
 *
 * No change to 53b, 53c/d/e, SBR-31, or audible PCM.
 * Never interpret output as proven normative FFmpeg QMF.
 */
#define SAUTI_AAC_SBR_SOURCE6_DELAY 6u
#define SAUTI_AAC_SBR_SOURCE6_SLOTS 32u

typedef struct {
    uint32_t ready;
    uint64_t frames;
    SautiAacSbrComplex previous_last6[6u*32u];
} SautiAacSbrTemporalSource6State;

typedef struct {
    uint32_t carried_from_previous;
    uint32_t current_source_slots;
    uint32_t missing_preroll;
    uint32_t previous_owner_slots;
    uint64_t frame_index;
} SautiAacSbrTemporalSource6Report;

/* The currently analyzed QMF must be [32][32], newest frame only.
 * Produces aligned LOW [32][32] and direct patch HIGH [32][64]
 * using source W[i-6]; the last six of current LOW are staged
 * for the NEXT frame. First frame has zeroed missing-preroll
 * slots, reported explicitly rather than silently called valid.
 *
 * All data, source geometry, states and outputs checked before
 * atomic publication. On any failure, state, outputs and report
 * remain unchanged. Arrays must be distinct from state; output
 * low/high arrays must not overlap.
 */
int sauti_aac_sbr_temporal_source6_prepare(
    const SautiAacSbrTemporalHistory *history,
    const SautiAacSbrComplex *current_low,size_t current_count,
    SautiAacSbrTemporalSource6State *state,
    SautiAacSbrComplex *aligned_low,size_t aligned_count,
    SautiAacSbrComplex *aligned_high,size_t high_count,
    SautiAacSbrTemporalSource6Report *report);

#endif
