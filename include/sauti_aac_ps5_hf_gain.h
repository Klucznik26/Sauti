#ifndef SAUTI_AAC_PS5_HF_GAIN_H
#define SAUTI_AAC_PS5_HF_GAIN_H

#include "sauti_aac_sbr_payload.h"

/* PS-5c2: diagnostic per-slot gain estimate from SBR-53a owned targets.
 * Operates on the raw 32x64 complex QMF, not adjusted QMF.
 * Does NOT implement SBR normative envelope averaging, limiter grouping,
 * smoothing, noise, sinusoidal injection, LPC or PS synthesis.
 * For zero source with positive target it records missing_source instead
 * of inventing energy. Output is analysis only, NEVER audible PCM.
 * Full output committed atomically on success. */
typedef struct {
    double gain[32u][64u];
    unsigned missing_source;
    double target_power;
    double raw_power;
    double predicted_signal_power;
    double noise_target;
    unsigned inherited_slots;
} SautiAacPs5HfGainReport;

int sauti_aac_ps5_hf_gain_analyze(
    const SautiAacSbrTemporalHfTargets *targets,
    const SautiAacSbrComplex *raw,size_t raw_count,
    SautiAacPs5HfGainReport *report);

#endif
