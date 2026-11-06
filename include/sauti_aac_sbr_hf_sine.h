#ifndef SAUTI_AAC_SBR_HF_SINE_H
#define SAUTI_AAC_SBR_HF_SINE_H

#include "sauti_aac_sbr_hf_noise.h"

/* HE-AAC SBR-19: independent complex QMF additional-sinusoid stage.
 * A flag is supplied for each SBR HIGH-frequency band. A sinusoid
 * appears only at that band's integer midpoint QMF subband.
 * Its four-phase sign follows classical SBR indexsine and subband
 * parity. Missing harmonic flags are normally read from the SBR
 * bitstream by the caller; this stage does not parse those bits.
 *
 * The per-channel phase and previous harmonic-presence map persist
 * across frames. A new harmonic starts at transient_env, whereas
 * one active in the preceding frame may continue in envelope zero.
 *
 * The sine amplitude for the target bin is sqrt(E/(1+Q)). E and Q
 * come from SBR-09 / SBR-10. This module does not implement boost,
 * QMF synthesis, noise suppression at sinusoid bins, or gain smoothing.
 */

typedef struct {
    uint32_t ready; /* Must be 0 or 1; initialize whole struct to zero */
    uint8_t phase;  /* 0..3 indexsine modulo 4 */
    uint8_t previous[SAUTI_AAC_SBR_HF_HIGH_BANDS];
} SautiAacSbrSineState;

/* Inputs:
 *   flags[high_band_count] are 0/1 bs_add_harmonic[i].
 *   transient_env 0..energy->env_count; env_count means NO newly
 *   appearing harmonics this frame (previous ones may continue).
 *   env_edges[env_count+1] and noise_edges[noise_count+1] strictly
 *   partition [0,slots], with noise borders subset of env borders.
 *   input/output are time-major complex QMF [slots][64].
 *
 * On failure both output and state are unchanged. Input/output may
 * alias. Caller must store independent state for each channel.
 * No allocation and no external decoder library. */
int sauti_aac_sbr_hf_sine_mix(
    const SautiAacSbrTables *tables,
    const SautiAacSbrEnergyFrame *energy,
    const uint8_t *harmonic_flags,
    unsigned flag_count,
    unsigned transient_env,
    const uint8_t *env_edges,
    const uint8_t *noise_edges,
    unsigned slots,
    const SautiAacSbrComplex *input,
    size_t input_count,
    SautiAacSbrComplex *output,
    size_t output_count,
    SautiAacSbrSineState *state);

#endif
