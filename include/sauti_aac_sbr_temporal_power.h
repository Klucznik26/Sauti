#ifndef SAUTI_AAC_SBR_TEMPORAL_POWER_H
#define SAUTI_AAC_SBR_TEMPORAL_POWER_H

#include "sauti_aac_sbr_temporal_qmf.h"

/* SBR-53c: true envelope-wise mean power of the PRE-ADJUSTMENT HF
 * baseline across QMF frame boundaries. The last envelope of one
 * payload may extend up to 6 QMF slots into the next AAC frame.
 * Those samples must participate in the SAME mean; do not compute
 * gains over a clipped frame-local envelope.
 *
 * The implementation reports COMPLETED envelope powers only and
 * stages an unfinished trailing envelope in state. A report may
 * include the just-completed previous-frame envelope first.
 * This is diagnostics / future limiter input, NOT gain application,
 * high-frequency synthesis, or playable PCM.
 */
typedef struct {
    uint8_t source;    /* 0: history.previous; 1: history.current */
    uint8_t envelope;  /* index within original source payload */
    uint8_t kx,k2;     /* active HF subband interval [kx,k2) */
    uint8_t slots;     /* number of QMF slots in full envelope */
    double mean_power[64]; /* average |X|^2 per QMF band */
} SautiAacSbrTemporalPowerEnvelope;

typedef struct {
    uint8_t completed_count;
    uint8_t deferred_slots;
    SautiAacSbrTemporalPowerEnvelope completed[SAUTI_AAC_SBR_MAX_ENV+1u];
} SautiAacSbrTemporalPowerReport;

typedef struct {
    uint32_t ready;
    uint64_t frames;
    uint8_t pending, pending_envelope;
    uint8_t pending_kx,pending_k2;
    uint8_t pending_count,pending_total,pending_remaining;
    double pending_sum[64];
} SautiAacSbrTemporalPowerState;

/* Input QMF comes from SBR-53b, in time-major [32][64] layout.
 * Exactly one call per successive temporal_history_push: if history
 * is on frame N, state must have consumed N-1 frames (or be zero
 * initialized for N=1). The state must be reset after seek.
 *
 * A pending cross-frame envelope is not emitted until all its
 * 32-slot frame and next-frame slots have been observed.
 * On any malformed owner, geometry, history, NaN/Inf QMF or
 * missing carry: neither report nor state is modified.
 */
int sauti_aac_sbr_temporal_power_measure(
    const SautiAacSbrTemporalHistory *history,
    const SautiAacSbrComplex *baseline_hf,size_t hf_count,
    SautiAacSbrTemporalPowerState *state,
    SautiAacSbrTemporalPowerReport *report);

#endif
