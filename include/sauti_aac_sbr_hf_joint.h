#ifndef SAUTI_AAC_SBR_HF_JOINT_H
#define SAUTI_AAC_SBR_HF_JOINT_H

#include "sauti_aac_sbr_limiter_bands.h"
#include "sauti_aac_sbr_hf_sine.h"

/* SBR-20: first integrated native HIGH-FREQUENCY QMF adjustment stage.
 * Combines automatic limiter bands (SBR-17), capped gain (SBR-16),
 * complex noise (SBR-18), and persistent sinusoids (SBR-19).
 *
 * IMPORTANT: this is a *testable joint model*, not yet normative
 * MPEG-4 SBR HF adjustment: it uses the non-standard eight-phase noise
 * generator, and does not smooth gains, account jointly for limiter
 * boost plus sine/noise before limiting, handle transient-specific
 * noise omission, or perform the analysis/synthesis QMF banks.
 *
 * Energy allocation of an ACTIVE harmonic subband:
 *   - random noise OFF (PRNG still advances in that band),
 *   - adjusted source HF power fraction Q/(1+Q),
 *   - sinusoid power E/(1+Q).
 * Other subbands follow SBR-18's signal E/(1+Q) and random-noise
 * E*Q/(1+Q) split. All squared-energy statements refer to expected
 * component energy, not finite-window squared magnitude after
 * correlated superposition. Limiter clipping can lower output power.
 */
typedef struct {
    const SautiAacSbrTables *tables;
    const SautiAacSbrPatches *patches;
    const SautiAacSbrEnergyFrame *energy;
    const uint8_t *harmonic_flags; /* n_high binary flags */
    unsigned flag_count;
    unsigned transient_env;       /* 0..env_count */
    const uint8_t *env_edges;     /* env_count+1, in QMF slots */
    const uint8_t *noise_edges;   /* noise_count+1, in QMF slots */
    unsigned slots;              /* 1..40 */
    unsigned limiter_mode;       /* 0..3 */
    unsigned gain_mode;          /* 0..3 */
} SautiAacSbrHfJointConfig;

typedef struct {
    SautiAacSbrNoiseState noise;
    SautiAacSbrSineState sine;
} SautiAacSbrHfJointState;

/* All pointer arguments must be distinct objects except for input
 * and output, which MAY alias. Input and output are arrays of complex
 * QMF coefficients in time-major [slots][64] layout; counts are
 * numbers of entries, not bytes.
 *
 * On failure output, report and BOTH histories are unchanged.
 * A caller initializes state to zero per channel, and preserves it
 * between frames. Does not parse a HE-AAC frame or synthesize PCM.
 */
int sauti_aac_sbr_hf_joint_process(
    const SautiAacSbrHfJointConfig *config,
    const SautiAacSbrComplex *input,
    size_t input_count,
    SautiAacSbrComplex *output,
    size_t output_count,
    SautiAacSbrHfJointState *state,
    SautiAacSbrLimiterReport *report);

#endif
