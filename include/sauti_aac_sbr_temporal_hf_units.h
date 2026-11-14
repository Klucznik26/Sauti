#ifndef SAUTI_AAC_SBR_TEMPORAL_HF_UNITS_H
#define SAUTI_AAC_SBR_TEMPORAL_HF_UNITS_H

#include "sauti_aac_sbr_temporal_noise.h"

/* SBR-54i: isolate a hypothesis about physical QMF amplitude units.
 *
 * MPEG-4 SBR envelope energies are typically interpreted using
 * an internal PCM scale of approximately +/-32768, whereas Sauti's
 * AAC-LC core supplies QMF input floats in approximately +/-1.
 *
 * As a DIAGNOSTIC ONLY, keep the original low QMF unchanged and
 * convert a separately reconstructed high QMF from the 16-bit
 * reference amplitude domain into the unit-float domain.
 *
 * high_float = high_sbr / 32768, for active HF bins only.
 *
 * This is NOT a normative SBR solution: gain calculation itself
 * still uses an unnormalized source/target pair, and QMF synthesis,
 * latency, per-envelope harmonics and smoothing need independent
 * conformance verification. Do NOT change the original PCM preview,
 * the native decoder, or SBR dequantized energies.
 *
 * On error output remains byte-for-byte unchanged. */
#define SAUTI_AAC_SBR_HF_DOMAIN_FACTOR 32768.0

int sauti_aac_sbr_temporal_hf_units_float(
    const SautiAacSbrComplex *high,size_t high_count,
    const SautiAacSbrTemporalNoiseFrame *noise,
    SautiAacSbrComplex *output,size_t output_count);

#endif
