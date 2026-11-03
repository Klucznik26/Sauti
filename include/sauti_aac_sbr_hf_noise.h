#ifndef SAUTI_AAC_SBR_HF_NOISE_H
#define SAUTI_AAC_SBR_HF_NOISE_H

#include "sauti_aac_sbr_hf_adjust.h"
#include <stddef.h>
#include <stdint.h>

/* SBR-18: deterministic complex high-band QMF noise with energy split.
 * This independent native module takes an ALREADY energy-matched
 * high-band signal (e.g. after SBR-15, or capped SBR-16) and the linear
 * envelope/noise factors from SBR-09/10.
 *
 * Q is a ratio: signal_gain = sqrt(1/(1+Q)), noise_amplitude =
 * sqrt(E_target*Q/(1+Q)).  For an energy-matched input, expected
 * output power is E_target because the random phase is zero-mean.
 * The generated complex noise has exactly unit magnitude per sample.
 *
 * IMPORTANT: this is not yet the normative 512-entry SBR noise ROM
 * and does not implement smoothing, sinusoidal harmonics, transient
 * suppression or normative limiter-aware noise boosting. It does not
 * generate QMF samples or PCM from AAC-LC. The caller must preserve
 * one SautiAacSbrNoiseState per independent channel.
 */

typedef struct {
    uint32_t ready; /* zero-initialize state before the first frame */
    uint32_t seed;  /* PRNG state; nonzero when ready */
    uint64_t emitted; /* total generated high-band complex samples */
} SautiAacSbrNoiseState;

/* Time-major [slots][64] input and output, both in QMF units.
 * env_edges[env_count+1] and noise_edges[noise_count+1] are already
 * expanded into QMF slot indices by the caller; both strictly partition
 * [0,slots]. Every internal noise border MUST coincide with an envelope
 * border. Noise frequency intervals come from tables->noise[].
 * All active energy and Q values are finite and >=0.
 *
 * A zero target envelope always produces zero at that QMF subband.
 * Positive target with completely zero source QMF in its envelope
 * cannot achieve the requested signal/noise ratio: fail closed.
 *
 * Output and input may alias; output/state may NOT overlap each other.
 * On error both output and state stay bit-identical. No allocation.
 */
int sauti_aac_sbr_hf_noise_mix(
    const SautiAacSbrTables *tables,
    const SautiAacSbrEnergyFrame *energy,
    const uint8_t *env_edges,
    const uint8_t *noise_edges,
    unsigned slots,
    const SautiAacSbrComplex *input,
    size_t input_count,
    SautiAacSbrComplex *output,
    size_t output_count,
    SautiAacSbrNoiseState *state);

#endif
