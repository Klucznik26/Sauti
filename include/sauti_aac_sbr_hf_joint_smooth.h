#ifndef SAUTI_AAC_SBR_HF_JOINT_SMOOTH_H
#define SAUTI_AAC_SBR_HF_JOINT_SMOOTH_H

#include "sauti_aac_sbr_hf_joint.h"
#include "sauti_aac_sbr_smooth.h"

/* SBR-22: integrate the SBR-21 coefficient-domain 5-tap smoothing
 * upstream of noise/harmonic signal assembly. SBR-20 remains unchanged.
 *
 * For each envelope and QMF bin compute total complex-signal gain
 * from SBR-16 limiter and the SBR-18/20 signal/noise energy split.
 * Smooth that GAIN and the noise AMPLITUDE across QMF time slots;
 * feed smoothed values to the SBR-18 phase/noise renderer and finally
 * append SBR-19 sinusoids. The sine itself is not smoothed.
 *
 * bs_smoothing_mode: 0 = five-tap FIR; 1 = bypass.
 * attack[env_count]: 0/1, bypasses FIR for marked transients,
 * but still advances the coefficient history.
 *
 * An all-zero-initialized state must be used for a new channel.
 * Both its noise/sine and gain/noise-coefficient histories persist
 * between frames, and are committed only if ALL stages succeed.
 * Output, report and state are bitwise unchanged on failure.
 * No heap, FFmpeg decoder, QMF filter bank or PCM output.
 */
typedef struct {
    SautiAacSbrHfJointState joint;
    SautiAacSbrSmoothState smooth;
} SautiAacSbrHfSmoothState;

int sauti_aac_sbr_hf_joint_process_smoothed(
    const SautiAacSbrHfJointConfig *config,
    const uint8_t *attack, /* [energy->env_count] */
    unsigned bs_smoothing_mode,
    const SautiAacSbrComplex *input,
    size_t input_count,
    SautiAacSbrComplex *output,
    size_t output_count,
    SautiAacSbrHfSmoothState *state,
    SautiAacSbrLimiterReport *report);

#endif
