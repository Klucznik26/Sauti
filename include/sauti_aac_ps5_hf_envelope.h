#ifndef SAUTI_AAC_PS5_HF_ENVELOPE_H
#define SAUTI_AAC_PS5_HF_ENVELOPE_H
#include "sauti_aac_ps5_hf_gain.h"

/* PS-5c3: owner/envelope mean-power and signal/noise budget preparation.
 * Groups QMF samples by (original SBR source, original envelope, bin).
 * Inherited slots use their previous frame's own envelope and targets.
 * This is a diagnostic/planning API, NOT a normative SBR HF renderer:
 * no limiter-band caps, coefficient smoothing, ROM noise or sine injected.
 * Values must not be converted to PCM until those stages exist. */
typedef struct {
    double provisional_gain[32u][64u];
    double signal_budget;
    double noise_budget;
    double raw_power;
    unsigned missing_source;
    unsigned harmonic_bins;
    unsigned inherited_slots;
    unsigned groups;
} SautiAacPs5HfEnvelopeReport;

int sauti_aac_ps5_hf_envelope_analyze(
    const SautiAacSbrTemporalHfTargets *targets,
    const SautiAacSbrComplex *raw,size_t raw_count,
    SautiAacPs5HfEnvelopeReport *report);
/* Experimental owner-aware wideband gain cap, not ISO f_tablelim.
 * Every (source, envelope) owner is limited independently.
 * Gains use normalized QMF PCM domain (1/32768 envelope amplitude).
 * Atomic output: on invalid input gains remains unchanged.
 * Use only for the --experimental-pcm-limited preview. */
int sauti_aac_ps5_hf_limit_preview(
    const SautiAacSbrTemporalHfTargets *targets,
    const SautiAacSbrComplex *raw,size_t raw_count,
    const SautiAacPs5HfEnvelopeReport *envelope,
    double gains[32u][64u]);
#endif
