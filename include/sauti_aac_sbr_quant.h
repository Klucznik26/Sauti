#ifndef SAUTI_AAC_SBR_QUANT_H
#define SAUTI_AAC_SBR_QUANT_H

#include "sauti_aac_sbr_control.h"
#include "sauti_aac_sbr_huffman.h"
#include <stddef.h>
#include <stdint.h>

/* HE-AAC SBR-08: decode the quantized envelope and noise-floor values.
 * This is NOT dequantization to energies, stereo unmapping or QMF synthesis.
 * The decoder never calls an external AAC library and never allocates. */
typedef struct {
    uint32_t env_count, noise_count;
    uint8_t freq_res[SAUTI_AAC_SBR_MAX_ENV];
    uint8_t env[SAUTI_AAC_SBR_MAX_ENV][SAUTI_AAC_SBR_MAX_MASTER_BANDS];
    uint8_t noise[2][SAUTI_AAC_SBR_MAX_NOISE_BANDS];
} SautiAacSbrQuantFrame;

/* Persistent channel history; the caller initializes it to all zeros.
 * A change of SBR frequency tables, coupling mode or amplitude resolution
 * requires new frequency-coded data before a time-differential envelope.
 * No implicit reuse across a changed QMF geometry is allowed. */
typedef struct {
    uint32_t has_env, has_noise;
    uint32_t prev_freq_res;
    uint32_t amp_res, balanced;
    uint8_t prev_env[SAUTI_AAC_SBR_MAX_MASTER_BANDS];
    uint8_t prev_noise[SAUTI_AAC_SBR_MAX_NOISE_BANDS];
    SautiAacSbrTables prev_tables;
} SautiAacSbrQuantState;

/* Input begins immediately at sbr_envelope() followed by sbr_noise().
 * amp_res: bs_amp_res (0=1.5dB, 1=3.0dB);
 * balanced: right channel of a COUPLED CPE (0/1, not PS).
 * Classic FIXFIX with one envelope forces 1.5dB resolution.
 * On ANY failure cursor, output and state remain bit-identical.
 * An uninitialized history must be zero-filled before its first use. */
int sauti_aac_sbr_quant_decode(const uint8_t *data, size_t size,
                                size_t *bit_offset,
                                const SautiAacSbrGrid *grid,
                                const SautiAacSbrTables *tables,
                                const SautiAacSbrControl *control,
                                unsigned amp_res, unsigned balanced,
                                SautiAacSbrQuantState *state,
                                SautiAacSbrQuantFrame *out);

/* CPE with bs_coupling=0 serializes both envelope sets before both noise
 * sets. Decode that grammar transactionally, preserving independent channel
 * histories and the exact same quantization rules as the single-channel API. */
int sauti_aac_sbr_quant_decode_cpe_uncoupled(
    const uint8_t *data,size_t size,size_t *bit_offset,
    const SautiAacSbrGrid grid[2],const SautiAacSbrTables *tables,
    const SautiAacSbrControl control[2],unsigned amp_res,
    SautiAacSbrQuantState state[2],SautiAacSbrQuantFrame out[2]);
#endif
