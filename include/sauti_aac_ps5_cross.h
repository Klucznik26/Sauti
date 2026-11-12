#ifndef SAUTI_AAC_PS5_CROSS_H
#define SAUTI_AAC_PS5_CROSS_H
#include "sauti_aac_ps5_hf_envelope.h"

/* PS-5c4a: complete mean source power across FIXVAR -> VARFIX.
 * Takes raw pre-adjusted QMF from BOTH adjacent AAC frames.
 * The previous SBR last envelope continues into current first slots.
 * This is an audit of original envelope support, not a PCM generator:
 * limiter / ROM noise / sinusoid and smoothing remain unimplemented.
 * On invalid data output is unchanged. */
typedef struct {
    unsigned deferred_slots;
    unsigned previous_slots_in_envelope;
    unsigned total_slots_in_envelope;
    unsigned missing_source_bins;
    unsigned harmonic_bins;
    double mean_source_power[64u];
    double provisional_gain[64u];
    double signal_budget;
    double noise_budget;
} SautiAacPs5CrossReport;

int sauti_aac_ps5_cross_envelope_analyze(
    const SautiAacSbrTemporalHistory *history,
    const SautiAacSbrTemporalHfTargets *current_targets,
    const SautiAacSbrComplex *previous_raw,size_t previous_count,
    const SautiAacSbrComplex *current_raw,size_t current_count,
    SautiAacPs5CrossReport *report);
#endif
