/* Native SBR dtdf / inverse-filtering syntax (MPEG-4 AAC SBR).
 * No decode of the delta-coded scale factors, Huffman data or PCM.
 * Commit only after complete parse, including count validation. */
#include "sauti_aac_sbr_control.h"
#include "sauti_aac_headers.h"
#include <stdint.h>
#include <string.h>

int sauti_aac_sbr_control_parse(const uint8_t *data, size_t size,
                                 size_t *bit_offset,
                                 const SautiAacSbrGrid *grid,
                                 const SautiAacSbrTables *tables,
                                 SautiAacSbrControl *out) {
    if (!data || !bit_offset || !grid || !tables || !out ||
        size > SIZE_MAX / 8u || *bit_offset > size * 8u ||
        grid->env_count < 1u || grid->env_count > SAUTI_AAC_SBR_MAX_ENV ||
        grid->noise_count != (grid->env_count == 1u ? 1u : 2u) ||
        tables->n_noise < 1u ||
        tables->n_noise > SAUTI_AAC_SBR_MAX_NOISE_BANDS ||
        tables->n_high < 1u ||
        tables->n_high > SAUTI_AAC_SBR_MAX_MASTER_BANDS ||
        tables->n_low != (tables->n_high + 1u) / 2u ||
        tables->n_noise > tables->n_low ||
        tables->kx < 1u || tables->kx > 32u ||
        tables->m < 1u || tables->m > 64u - tables->kx) return 0;

    for (unsigned i = 0; i < grid->env_count; ++i)
        if (grid->freq_res[i] > 1u) return 0;

    size_t next_pos = *bit_offset;
    SautiAacSbrControl next;
    uint32_t value = 0;
    memset(&next, 0, sizeof(next));
    next.env_count = grid->env_count;
    next.noise_count = grid->noise_count;
    next.noise_band_count = tables->n_noise;

    /* sbr_dtdf(): envelope time/frequency delta flags. */
    for (unsigned i = 0; i < grid->env_count; ++i) {
        if (!sauti_aac_bits_read_asm(data, size, &next_pos, 1u, &value))
            return 0;
        next.df_env[i] = (uint8_t)value;
    }
    for (unsigned i = 0; i < grid->noise_count; ++i) {
        if (!sauti_aac_bits_read_asm(data, size, &next_pos, 1u, &value))
            return 0;
        next.df_noise[i] = (uint8_t)value;
    }
    /* sbr_invf(): one 2-bit mode per noise frequency band. */
    for (unsigned i = 0; i < tables->n_noise; ++i) {
        if (!sauti_aac_bits_read_asm(data, size, &next_pos, 2u, &value))
            return 0;
        next.invf_mode[i] = (uint8_t)value;
    }

    *out = next;
    *bit_offset = next_pos;
    return 1;
}
