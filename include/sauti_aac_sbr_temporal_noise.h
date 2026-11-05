#ifndef SAUTI_AAC_SBR_TEMPORAL_NOISE_H
#define SAUTI_AAC_SBR_TEMPORAL_NOISE_H

#include "sauti_aac_sbr_temporal_apply.h"
#include "sauti_aac_sbr_noise_rom.h"

/* SBR-53f: align the canonical 512-vector SBR noise ROM with the
 * ONE-FRAME-DELAYED QMF emission from SBR-53e.
 *
 * A frame's complete SBR-53a per-slot HF targets are retained while
 * SBR-53e holds its QMF samples. Noise ROM advances only when that
 * frame is emitted; both the bin geometry and envelope/noise targets
 * come from its ORIGINAL temporal owners, not the current frame.
 *
 * raw_amplitude = sqrt(E) * sqrt(Q/(1+Q)) is an UNSMOOTHED diagnostic
 * target, not final normative noise. This module deliberately does
 * NOT suppress sinusoids, use transient gates, smooth coefficients,
 * mix complex QMF, perform 64-QMF synthesis or produce PCM.
 * ROM phases DO advance through active bins with amplitude zero.
 * Each channel must have an independent, zero-initialized state.
 *
 * On failure state, output and report remain byte-for-byte unchanged.
 */
typedef struct {
    SautiAacSbrComplex vector[32u*64u];
    double raw_amplitude[32u*64u];
    uint8_t active[32u*64u]; /* 1 even if amplitude is zero;
                               * inactive bins have zero vector/amp */
    uint8_t smooth_bypass[32u]; /* per-slot original header/attack choice */
} SautiAacSbrTemporalNoiseFrame;

typedef struct {
    uint8_t emitted;
    uint64_t frame_index;    /* meaningful only if emitted */
    uint32_t vectors;        /* active HF QMF bins visited */
    uint32_t nonzero_amplitudes;
    uint32_t rom_index;      /* last emitted ROM index */
} SautiAacSbrTemporalNoiseReport;

typedef struct {
    uint32_t ready,closed;
    uint64_t frames,emitted_frames;
    SautiAacSbrNoiseRomState rom;
    SautiAacSbrTemporalHfTargets held_targets;
} SautiAacSbrTemporalNoiseState;

/* Call AFTER a successful SBR-53e apply() using the same frame's
 * history and apply_report. First call stages targets but does not
 * write the output frame; next call emits the preceding frame.
 * Outputs, state, report and history must have distinct storage. */
int sauti_aac_sbr_temporal_noise_prepare(
    const SautiAacSbrTemporalHistory *history,
    const SautiAacSbrTemporalApplyReport *apply_report,
    SautiAacSbrTemporalNoiseState *state,
    SautiAacSbrTemporalNoiseFrame *output,
    SautiAacSbrTemporalNoiseReport *report);

/* Call only AFTER successful SBR-53e apply_flush(). Emits the final
 * held frame, then closes the state. Repeated flushes are rejected. */
int sauti_aac_sbr_temporal_noise_flush(
    const SautiAacSbrTemporalApplyReport *apply_report,
    SautiAacSbrTemporalNoiseState *state,
    SautiAacSbrTemporalNoiseFrame *output,
    SautiAacSbrTemporalNoiseReport *report);

#endif
