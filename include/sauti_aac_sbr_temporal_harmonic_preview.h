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

/* SBR-69 strictly diagnostic A/B: add the same tone at oscillator phase+2.
 * Noise suppression, gain, onset/owner checks, 4-slot state clock and
 * phase history are IDENTICAL to the original SBR-54h preview.
 * This is not a normative HE-AAC oscillator fix. Use independent state
 * and independent 64-QMF synthesis for an actual PCM comparison. */
int sauti_aac_sbr_temporal_harmonic_preview_phase2(
    const SautiAacSbrTemporalHfTargets *targets,
    const uint8_t onset_allowed[32],
    const SautiAacSbrTemporalNoiseFrame *noise,
    const SautiAacSbrTemporalNoiseReport *noise_report,
    const SautiAacSbrComplex *raw_mix,size_t raw_count,
    SautiAacSbrComplex *output,size_t output_count,
    SautiAacSbrTemporalHarmonicPreviewState *state,
    SautiAacSbrTemporalHarmonicPreviewReport *report);

/* Put an already-built unscaled tone into the gain-adjusted QMF
 * domain. boost[i] is the limiter boost for that slot and band.
 * apply[i] is 1 only where that boost is known. A bin whose sine
 * sample is still the raw mix had no tone added, so it is copied.
 * Elsewhere output = gain + (sine-gain) * boost * 1024/sqrt(2).
 * The unscaled preview above is not modified. On failure *output
 * is unchanged. */
int sauti_aac_sbr_temporal_harmonic_scale(
    const SautiAacSbrComplex *gain,size_t gain_count,
    const SautiAacSbrComplex *raw,size_t raw_count,
    const SautiAacSbrComplex *sine,size_t sine_count,
    const double boost[32u*64u],
    const uint8_t apply[32u*64u],
    SautiAacSbrComplex *output,size_t output_count);

#endif
