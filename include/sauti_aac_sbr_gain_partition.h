#ifndef SAUTI_AAC_SBR_GAIN_PARTITION_H
#define SAUTI_AAC_SBR_GAIN_PARTITION_H

/* SBR-54j: source-verified SBR HF/noise/sine PRE-LIMITER allocation.
 *
 * Following reference MPEG-4 SBR pre-limiter equations used by
 * FFmpeg libavcodec/aacsbr.c:sbr_gain_calc():
 *
 *   temp = E / (1 + Q)
 *   Q_M  = sqrt(temp * Q)
 *   S_M  = sqrt(temp * sine_flag)
 *
 *   no sinusoid:
 *     G = sqrt(E / ((1 + E_curr)*(1 + Q*delta)))
 *   sinusoid active:
 *     G = sqrt(E*Q / ((1 + E_curr)*(1 + Q)))
 *
 * delta=0 for an envelope at the temporal attack, otherwise 1.
 * These outputs are *before* limiter cap, noise cap and boost,
 * smoothing and final synthesis. No PCM path is modified here.
 *
 * E, Q and E_curr must be in a consistent internal SBR amplitude
 * domain. This function does NOT convert float/16-bit domains and
 * does NOT declare end-to-end HE-AAC conformance.
 * On failure, *out is unchanged.
 */
typedef struct {
    double hf_gain;
    double noise_amplitude;
    double sine_amplitude;
} SautiAacSbrGainPartition;

int sauti_aac_sbr_gain_partition(
    double envelope_energy,
    double current_hf_power,
    double noise_factor,
    unsigned sinusoid_active,
    unsigned delta,
    SautiAacSbrGainPartition *out);

#endif
