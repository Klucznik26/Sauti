#ifndef SAUTI_AAC_SBR_TEMPORAL_MIX_H
#define SAUTI_AAC_SBR_TEMPORAL_MIX_H

#include "sauti_aac_sbr_temporal_noise.h"

/* SBR-53g: PREVIEW of raw SBR noise vector mixing with the delayed
 * gain-adjusted HF QMF emitted by SBR-53e.
 *
 * Preview[t,k] = AdjustedHF[t,k] +
 *                NoiseROM[t,k] * RawAmplitude[t,k].
 *
 * The active mask from SBR-53f is authoritative, including at zero
 * amplitude. We verify every active ROM value against the canonical
 * 512-vector table and verify the frame-index handoff.
 *
 * THIS IS NOT NORMATIVE HE-AAC AUDIO: transient/harmonic suppression,
 * the 5-tap gain/noise smoothing, sine mixing, predictive chirp and
 * 64-band synthesis remain separate future stages. Do not feed
 * preview samples directly to a production PCM output.
 *
 * Pure function. On ANY error neither output nor report changes.
 * Inputs/output must be distinct memory regions.
 */
typedef struct {
    uint64_t frame_index;
    uint32_t active_bins;
    uint32_t noise_bins;
    double noise_rms; /* RMS magnitude of the additive complex noise */
} SautiAacSbrTemporalMixReport;

int sauti_aac_sbr_temporal_noise_mix_preview(
    const SautiAacSbrComplex *adjusted_hf,size_t input_count,
    const SautiAacSbrTemporalNoiseFrame *noise,
    const SautiAacSbrTemporalApplyReport *apply_report,
    const SautiAacSbrTemporalNoiseReport *noise_report,
    SautiAacSbrComplex *output,size_t output_count,
    SautiAacSbrTemporalMixReport *report);

#endif
