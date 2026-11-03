#ifndef SAUTI_AAC_SBR_HF_JOINT_ROM_H
#define SAUTI_AAC_SBR_HF_JOINT_ROM_H

#include "sauti_aac_sbr_hf_joint_smooth.h"
#include "sauti_aac_sbr_noise_rom.h"

/* SBR-24: use the 512-vector MPEG-4 SBR noise ROM in the fully
 * assembled HF limiter -> coefficient smoothing -> noise -> sinusoid
 * chain. This is a NEW entry point; the SBR-20/22 interfaces and
 * their original xorshift / 8-phase noise are not modified.
 *
 * Each independent AAC channel owns a zero-initialized state.
 * The ROM noise index advances in every high QMF subband, including
 * those with zero noise amplitude due to an active sinusoid.
 * It wraps modulo 512 and survives frame boundaries. Smooth history
 * (four QMF slots) and SBR-19 sine phase also survive frame boundaries.
 *
 * All caller output/state/report writes are atomic. The input QMF
 * is precomputed/generated high-band signal; the function does NOT
 * perform HE-AAC bitstream decoding, QMF analysis/synthesis or PCM.
 * Limiter/noise/sine energy model still needs normative refinement.
 */
typedef struct {
    SautiAacSbrNoiseRomState noise_rom;
    SautiAacSbrSineState sine;
    SautiAacSbrSmoothState smooth;
} SautiAacSbrHfRomState;

int sauti_aac_sbr_hf_joint_process_rom(
    const SautiAacSbrHfJointConfig *config,
    const uint8_t *attack, /* per-envelope 0/1 transient flags */
    unsigned bs_smoothing_mode,
    const SautiAacSbrComplex *input,
    size_t input_count,
    SautiAacSbrComplex *output,
    size_t output_count,
    SautiAacSbrHfRomState *state,
    SautiAacSbrLimiterReport *report);

#endif
