#ifndef SAUTI_AAC_SBR_ENERGY_H
#define SAUTI_AAC_SBR_ENERGY_H

#include "sauti_aac_sbr_quant.h"

/* HE-AAC v1 SBR-09: dequantized (linear-domain) energies and noise-floor
 * factors. Values are double precision to preserve the entire classical
 * quantized value domain without intermediate floating-point overflow.
 * This is not QMF spectral shaping, limiter gain, or output PCM. */
typedef struct {
    uint32_t env_count;
    uint32_t noise_count;
    uint8_t freq_res[SAUTI_AAC_SBR_MAX_ENV];
    double env_energy[SAUTI_AAC_SBR_MAX_ENV][SAUTI_AAC_SBR_MAX_MASTER_BANDS];
    double noise_factor[2][SAUTI_AAC_SBR_MAX_NOISE_BANDS];
} SautiAacSbrEnergyFrame;

/* Stateless conversion for one independent SBR channel, including an
 * uncoupled stereo channel. The FIXFIX/one-envelope class forces fine
 * 1.5-dB steps. Balanced/right coupled stereo REQUIRES paired-channel
 * unmapping and is explicitly rejected; it is never silently decoded
 * as independent channel energy.
 *
 * E = 64 * 2^(q * (amp_res ? 1 : 0.5))
 * Q = 2^(6 - noise_q)
 *
 * All input arrays/metadata must be valid and mutually consistent.
 * Returns zero without writing *out for any error, and allocates nothing. */
int sauti_aac_sbr_energy_dequant(const SautiAacSbrQuantFrame *quant,
                                  const SautiAacSbrGrid *grid,
                                  const SautiAacSbrTables *tables,
                                  unsigned amp_res,
                                  unsigned balanced,
                                  SautiAacSbrEnergyFrame *out);


/* SBR-10: joint dequantization of a *coupled* CPE pair.
 * The first quant frame is the level/combined-energy channel decoded
 * with balanced=0 by SBR-08; the second is the balance channel decoded
 * with balanced=1. They share exactly the same classic SBR time/frequency
 * grids and amp resolution. This API performs the pan/noise unmapping.
 * The calling parser MUST preserve the channel roles; quant frames
 * themselves don't embed a coupling flag.
 *
 * Combined envelope E = 128 * 2^(alpha * q_level)
 * Pan ratio R = 2^(alpha * (pan_offset - q_balance)),
 * alpha=0.5, pan_offset=24 in 1.5dB mode;
 * alpha=1, pan_offset=12 in 3dB mode.
 * E_left = E/(1+R); E_right = E*R/(1+R).
 *
 * Combined noise Q = 2^(7 - q_noise_level)
 * Noise ratio Rn = 2^(12 - q_noise_balance);
 * Q_left = Q/(1+Rn); Q_right = Q*Rn/(1+Rn).
 *
 * If invalid, neither output changes. left_out and right_out MUST
 * be distinct. No external codec, allocation, or QMF synthesis. */
int sauti_aac_sbr_coupled_dequant(
    const SautiAacSbrQuantFrame *level,
    const SautiAacSbrQuantFrame *balance,
    const SautiAacSbrGrid *grid,
    const SautiAacSbrTables *tables,
    unsigned amp_res,
    SautiAacSbrEnergyFrame *left_out,
    SautiAacSbrEnergyFrame *right_out);

#endif
