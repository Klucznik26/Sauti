#ifndef SAUTI_AAC_SBR_TEMPORAL_REFERENCE_GAIN_H
#define SAUTI_AAC_SBR_TEMPORAL_REFERENCE_GAIN_H

#include "sauti_aac_sbr_gain_limit_partition.h"
#include "sauti_aac_sbr_temporal_gain.h"

/* SBR-54l: read-only adapter of the tested 54j+54k pre-limiter
 * and limiter/boost math to exactly the COMPLETED ORIGINAL SBR
 * envelopes from the SBR-53c temporal power report.
 *
 * For FIXVAR, source=0 belongs to history.previous; source=1
 * belongs to history.current. No output for pending envelopes.
 * The currently parsed harmonic center flags and transient_env
 * are used as diagnostic control inputs. Envelope energy and the
 * current are lifted by 2^30 at the limiter call. Noise is not.
 * The current is the HF-generator window [start-6, end-6) of the
 * owner frame. current_hf is that owner when source is 1, and
 * previous_hf is that owner when source is 0. A missing frame
 * keeps the unshifted SBR-53c mean for that envelope. Square sums
 * stay on the unshifted mean. The sine midpoint and the
 * envelope-band map are passed separately. FFmpeg's delayed e_a,
 * active sine gating and synthesis time shift are not yet used:
 * this is a reference-MATH comparison, NOT conformant HE-AAC.
 *
 * Legacy SBR-53d, 54a, and decoder states are unmodified.
 * If any envelope fails validation, *out remains bitwise unchanged.
 */
typedef struct {
    uint8_t source,envelope,slots,kx,k2,limiter_count;
    uint8_t transient_delta;
    uint32_t harmonic_bins;
    SautiAacSbrGainLimitedFrame limited;
    double old_gain_sq_sum,new_gain_sq_sum;
} SautiAacSbrTemporalReferenceGainEnvelope;

typedef struct {
    uint8_t completed_count;
    uint32_t gain_changed_bins,noise_positive_bins;
    uint32_t sine_bins,group_count,noise_capped_bins;
    double old_gain_sq_sum,new_gain_sq_sum;
    SautiAacSbrTemporalReferenceGainEnvelope
        completed[SAUTI_AAC_SBR_MAX_ENV+1u];
} SautiAacSbrTemporalReferenceGainReport;

int sauti_aac_sbr_temporal_reference_gain_calculate(
    const SautiAacSbrTemporalHistory *history,
    const SautiAacSbrTemporalPowerReport *power,
    const SautiAacSbrComplex *current_hf,size_t current_count,
    const SautiAacSbrComplex *previous_hf,size_t previous_count,
    const SautiAacSbrComplex *older_hf,size_t older_count,
    SautiAacSbrTemporalReferenceGainReport *out);

#endif
