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

#endif
