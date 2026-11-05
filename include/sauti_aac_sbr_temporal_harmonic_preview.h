#ifndef SAUTI_AAC_SBR_TEMPORAL_HARMONIC_PREVIEW_H
#define SAUTI_AAC_SBR_TEMPORAL_HARMONIC_PREVIEW_H

#include "sauti_aac_sbr_temporal_mix.h"

/* SBR-54h: isolated delayed-QMF sinusoidal/harmonic PREVIEW.
 *
 * Retains the SBR-19 4-phase complex oscillator and k parity,
 * but accepts per-slot original HF target owners from SBR-53a
 * and original onset gate (env>=transient_env, or continued tone)
 * to handle FIXVAR cross-frame handoffs.
 *
 * The ordinary SBR-53g PREVIEW is left byte-for-byte unchanged.
 * For an active tonal bin, subtract its raw SBR-53f ROM noise
 * and add sqrt(E/(1+Q))*i^phase (parity-dependent imaginary sign).
 * This is NOT normative HE-AAC: it lacks joint HF gain/sine/noise
 * allocation, smoothing, transient boost and calibrated DSP levels.
 * Suitable only for diagnostic A/B against an independent decoder.
 *
 * One state per channel; an emitted frame must use contiguous frame
 * indexes including one-frame delayed processing and final flush.
 * On any failure *state, *report, *output remain unchanged.
 */
typedef struct {
    uint64_t frames;
    uint8_t phase,ready;
    uint8_t previous[64u];
} SautiAacSbrTemporalHarmonicPreviewState;

typedef struct {
    uint64_t frame_index;
    uint32_t marked_bins,active_tones,gated_noise_bins;
    double tonal_power,removed_noise_power;
} SautiAacSbrTemporalHarmonicPreviewReport;

/* onset_allowed[t] is computed from the original retained payload
 * corresponding to each targets.slot[t] BEFORE its one-frame delay:
 *   (slot.envelope >= payload.transient_env) ? 1 : 0
 * Existing previous active tones may continue when it is zero.
 * Input arrays 32x64, output 32x64. */
int sauti_aac_sbr_temporal_harmonic_preview(
    const SautiAacSbrTemporalHfTargets *targets,
    const uint8_t onset_allowed[32],
    const SautiAacSbrTemporalNoiseFrame *noise,
    const SautiAacSbrTemporalNoiseReport *noise_report,
    const SautiAacSbrComplex *raw_mix,size_t raw_count,
    SautiAacSbrComplex *output,size_t output_count,
    SautiAacSbrTemporalHarmonicPreviewState *state,
    SautiAacSbrTemporalHarmonicPreviewReport *report);

#endif
