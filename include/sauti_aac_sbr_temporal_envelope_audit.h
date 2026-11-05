#ifndef SAUTI_AAC_SBR_TEMPORAL_ENVELOPE_AUDIT_H
#define SAUTI_AAC_SBR_TEMPORAL_ENVELOPE_AUDIT_H

#include "sauti_aac_sbr_temporal_energy_audit.h"

/* SBR-54f: per-ORIGINAL-envelope, per-QMF-bin temporal energy budget.
 * An energy target describes an envelope MEAN, not a per-slot peak.
 *
 * A caller allocates groups[max_frames*SAUTI_AAC_SBR_MAX_ENV] and
 * zero-initializes them. A source=0 temporal owner belongs to
 * emitted_frame-1; source=1 belongs to emitted_frame.
 * Thus FIXVAR carry-over contributes to the same original envelope.
 *
 * Retains no DSP state. This is DIAGNOSTIC ONLY: the individual
 * uncorrelated HF/noise power sum is not normative MPEG-4 SBR
 * synthesis energy (cross terms, sine gating, phase and chirp missing).
 */
typedef struct {
    uint8_t used;
    uint32_t count[64];
    double target_per_slot[64];
    double target_total[64], hf_total[64], noise_total[64];
} SautiAacSbrTemporalEnvelopeAuditGroup;

typedef struct {
    SautiAacSbrTemporalEnvelopeAuditGroup *groups;
    size_t max_frames;
    uint64_t next_emitted_frame;
} SautiAacSbrTemporalEnvelopeAuditState;

typedef struct {
    uint64_t completed_frames;
    uint32_t envelopes;
    uint32_t active_envelope_bins;
    uint32_t above_target,above_twice,above_tenfold;
    uint64_t total_slot_bins;
    double target_total,hf_total,noise_total;
    double max_ratio;
    uint64_t worst_frame;
    uint32_t worst_envelope,worst_bin,worst_slots;
} SautiAacSbrTemporalEnvelopeAuditReport;

/* Pure validation then atomic update: on any error all groups and
 * next_emitted_frame remain unchanged. Never alias input arrays
 * with the caller's groups. */
int sauti_aac_sbr_temporal_envelope_audit_push(
    const SautiAacSbrTemporalHfTargets *targets,
    const SautiAacSbrComplex *adjusted_hf,size_t adjusted_count,
    const SautiAacSbrTemporalNoiseFrame *noise,
    const SautiAacSbrTemporalApplyReport *apply,
    const SautiAacSbrTemporalNoiseReport *noise_report,
    SautiAacSbrTemporalEnvelopeAuditState *state);

/* Summarizes only envelopes observed in emitted frames. The caller
 * must separately verify completed_frames==expected frame count. */
int sauti_aac_sbr_temporal_envelope_audit_report(
    const SautiAacSbrTemporalEnvelopeAuditState *state,
    SautiAacSbrTemporalEnvelopeAuditReport *out);

#endif
