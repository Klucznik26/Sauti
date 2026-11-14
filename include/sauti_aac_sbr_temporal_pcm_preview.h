#ifndef SAUTI_AAC_SBR_TEMPORAL_PCM_PREVIEW_H
#define SAUTI_AAC_SBR_TEMPORAL_PCM_PREVIEW_H

#include "sauti_aac_sbr_temporal_mix.h"
#include "sauti_aac_sbr_qmf_synth_mod.h"

/* SBR-54a: one-frame-delayed 64-band QMF PCM PREVIEW.
 *
 * SBR-53b supplies 32 low QMF bins from the present AAC core.
 * SBR-53e/g emit HIGH QMF bins from the PREVIOUS core, after
 * cross-frame envelope gains and RAW ROM noise have been prepared.
 *
 * Retain the matching low QMF frame and combine:
 *   [0,kx)        = retained low from the same original core frame
 *   [kx,k2)       = emitted high HF preview selected per-slot
 *   [k2,64)       = zero
 * Then run the existing 64-band QMF synthesis with continuous
 * per-channel filter-bank history. SBR-53f's active mask is
 * authoritative for per-slot [kx,k2) geometry, including changes
 * of original SBR owner in a FIXVAR overlap.
 *
 * This gives REAL float QMF waveform samples, 2048 per emitted
 * frame, but is NOT a conformant HE-AAC decode. It still lacks
 * proper sinusoid/harmonic assembly, gain/noise smoothing in the
 * final mixer, automatic LPC/chirp and normative alignment/latency.
 * Never substitute it for the production audio decoder.
 *
 * State and all caller-visible outputs are atomic per call. This
 * module does not own the external AAC core, noise, gain or ROM
 * histories: their rollback must be handled by a future unified
 * transaction driver. No external codec, allocation or I/O.
 */
typedef struct {
    uint32_t ready,closed; /* zero-init for a new channel */
    uint64_t frames,emitted_frames;
    SautiAacSbrComplex held_low[32u*32u];
    SautiAacSbrQmfSynthWindowState synth;
} SautiAacSbrTemporalPcmPreviewState;

typedef struct {
    uint8_t emitted;
    uint64_t frame_index; /* valid only if emitted */
    uint32_t samples,nonzero;
    double rms,peak;
} SautiAacSbrTemporalPcmPreviewReport;

/* On the first frame apply_report->emitted=0, which only stages
 * current_low: high/noise/mix_report may be NULL; pcm untouched.
 * Thereafter apply_report->emitted=1 and ALL high-side arguments
 * must be valid, referring to frame_index=state.frames-1.
 * On any failure state, output and report are untouched. */
int sauti_aac_sbr_temporal_pcm_preview_push(
    const SautiAacSbrComplex *current_low,size_t low_count,
    const SautiAacSbrComplex *emitted_high,size_t high_count,
    const SautiAacSbrTemporalNoiseFrame *noise,
    const SautiAacSbrTemporalApplyReport *apply_report,
    const SautiAacSbrTemporalMixReport *mix_report,
    SautiAacSbrTemporalPcmPreviewState *state,
    float *pcm,size_t pcm_count,
    SautiAacSbrTemporalPcmPreviewReport *report);

/* Only after SBR-53e/f/g finish their normal final-frame flush.
 * No implicit tail padding or zeroing of deferred envelope slots.
 * Repeated flush, mismatched frame or invalid input is rejected. */
int sauti_aac_sbr_temporal_pcm_preview_flush(
    const SautiAacSbrComplex *emitted_high,size_t high_count,
    const SautiAacSbrTemporalNoiseFrame *noise,
    const SautiAacSbrTemporalApplyReport *apply_report,
    const SautiAacSbrTemporalMixReport *mix_report,
    SautiAacSbrTemporalPcmPreviewState *state,
    float *pcm,size_t pcm_count,
    SautiAacSbrTemporalPcmPreviewReport *report);

#endif
