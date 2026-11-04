#ifndef SAUTI_AAC_SBR_TEMPORAL_GAIN_H
#define SAUTI_AAC_SBR_TEMPORAL_GAIN_H

#include "sauti_aac_sbr_temporal_power.h"
#include "sauti_aac_sbr_limiter_bands.h"

/* SBR-53d: compute capped limiter gains from COMPLETE HF envelope
 * mean powers reported by SBR-53c, including any 2..6 QMF slots
 * inherited from the previous AAC frame. Values belong to the
 * ORIGINAL SBR envelope, not to a clipped AAC frame window.
 *
 * This is side-data only. It does not scale complex QMF, apply
 * smoothing, noise or sine waves, synthesize PCM, or mark a frame
 * playable. Gain/noise/tonal combination is still a later stage.
 * Finite, positive signaled noise Q allows an unamplifiable zero
 * source band; zero source is never replaced with invented HF power.
 */
typedef struct {
    uint8_t source,envelope,slots,kx,k2;
    uint8_t limiter_count;
    uint8_t noise_only_bands;
    double limiter_max[SAUTI_AAC_SBR_MAX_LIMITER_BANDS];
    double limiter_boost[SAUTI_AAC_SBR_MAX_LIMITER_BANDS];
    double applied_gain[64];
} SautiAacSbrTemporalGainEnvelope;

typedef struct {
    uint8_t completed_count;
    SautiAacSbrTemporalGainEnvelope
        completed[SAUTI_AAC_SBR_MAX_ENV+1u];
} SautiAacSbrTemporalGainReport;

/* Requires a matching temporal SBR history and SBR-53c report for
 * that same frame. Verify source, original envelope identity,
 * complete slot length and every nonnegative finite QMF power.
 * On any error *out is unchanged (no persistent DSP state exists).
 * No heap and no external audio libraries. */
int sauti_aac_sbr_temporal_gain_calculate(
    const SautiAacSbrTemporalHistory *history,
    const SautiAacSbrTemporalPowerReport *power,
    SautiAacSbrTemporalGainReport *out);

#endif
