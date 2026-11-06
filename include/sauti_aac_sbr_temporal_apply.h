#ifndef SAUTI_AAC_SBR_TEMPORAL_APPLY_H
#define SAUTI_AAC_SBR_TEMPORAL_APPLY_H

#include "sauti_aac_sbr_temporal_gain.h"

/* SBR-53e: apply completed SBR-53d envelope gains to their ORIGINAL
 * QMF slots with a one-AAC-frame staging delay. A FIXVAR envelope
 * crossing the boundary is NOT applied until its future QMF samples
 * have finalized the single full-envelope gain. No guessed gains,
 * clipping of SBR edges, noise generation, smoothing or PCM output.
 *
 * This is an isolated HF-baseline experiment. Do not feed its output
 * directly to a final 64-band QMF synthesizer: noise, sinusoids,
 * smoothing, chirp and normative alignment remain unimplemented here.
 * On error all output buffers, reports and state remain unchanged.
 */
typedef struct {
    uint32_t ready, closed; /* zero-initialize; closed after flush */
    uint64_t frames;
    uint8_t pending_slots; /* unassigned last slots of held frame */
    uint8_t known[32];
    SautiAacSbrComplex held[32u*64u]; /* PRE-adjustment QMF */
    double slot_gain[32u][64u];
} SautiAacSbrTemporalApplyState;

typedef struct {
    uint8_t emitted;      /* 0 on initial staging, 1 thereafter */
    uint8_t pending_slots;
    uint64_t frame_index; /* valid only when emitted=1 */
} SautiAacSbrTemporalApplyReport;

/* One call per consecutive temporal_history_push() and SBR-53d
 * gain calculation. baseline_hf is time-major [32][64].
 * adjusted_hf is [32][64] and is written only if report.emitted=1;
 * on the first input frame it stays byte-for-byte untouched.
 * Never alias input, output, history or state allocations. */
int sauti_aac_sbr_temporal_gain_apply(
    const SautiAacSbrTemporalHistory *history,
    const SautiAacSbrComplex *baseline_hf,size_t baseline_count,
    const SautiAacSbrTemporalGainReport *gains,
    SautiAacSbrTemporalApplyState *state,
    SautiAacSbrComplex *adjusted_hf,size_t adjusted_count,
    SautiAacSbrTemporalApplyReport *report);

/* Emit the final held frame at end of stream. Refuse a still-open
 * deferred envelope; do NOT silently truncate it or replace it with
 * silence. A successful flush closes the state until reinitialized. */
int sauti_aac_sbr_temporal_gain_apply_flush(
    SautiAacSbrTemporalApplyState *state,
    SautiAacSbrComplex *adjusted_hf,size_t adjusted_count,
    SautiAacSbrTemporalApplyReport *report);

#endif
