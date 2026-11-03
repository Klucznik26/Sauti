/* SBR-09: classical HE-AAC spectral envelope and noise dequantization.
 * No external decoder; self-contained arithmetic with libm for ldexp().
 * Coupled stereo MUST be handled by a separate joint-channel unmapper.
 * Commit output only after all data have been fully validated. */
#include "sauti_aac_sbr_energy.h"
#include <math.h>
#include <string.h>

static int valid_tables(const SautiAacSbrTables *t) {
    if (!t || t->kx < 1u || t->kx > 32u ||
        t->m < 1u || t->m > 64u - t->kx ||
        t->n_high < 1u || t->n_high > SAUTI_AAC_SBR_MAX_MASTER_BANDS ||
        t->n_low != (t->n_high + 1u) / 2u ||
        t->n_noise < 1u || t->n_noise > SAUTI_AAC_SBR_MAX_NOISE_BANDS ||
        t->n_noise > t->n_low ||
        t->high[0] != t->kx || t->high[t->n_high] != t->kx + t->m ||
        t->low[0] != t->kx || t->low[t->n_low] != t->kx + t->m ||
        t->noise[0] != t->kx ||
        t->noise[t->n_noise] != t->kx + t->m)
        return 0;

    for (unsigned j = 1u; j <= t->n_high; ++j)
        if (t->high[j] <= t->high[j - 1u]) return 0;
    for (unsigned j = 1u; j <= t->n_low; ++j)
        if (t->low[j] <= t->low[j - 1u]) return 0;
    for (unsigned j = 1u; j <= t->n_noise; ++j)
        if (t->noise[j] <= t->noise[j - 1u]) return 0;
    /* Low-frequency band boundaries must be a consistent merge of high. */
    const unsigned odd = t->n_high & 1u;
    for (unsigned j = 0u; j <= t->n_low; ++j) {
        const unsigned idx = j ? 2u * j - odd : 0u;
        if (idx > t->n_high || t->low[j] != t->high[idx]) return 0;
    }
    /* Noise boundaries must coincide with some low-band edge. */
    for (unsigned j = 0u; j <= t->n_noise; ++j) {
        unsigned k = 0u;
        while (k <= t->n_low && t->low[k] != t->noise[j]) ++k;
        if (k > t->n_low) return 0;
    }
    return 1;
}

int sauti_aac_sbr_energy_dequant(const SautiAacSbrQuantFrame *quant,
                                  const SautiAacSbrGrid *grid,
                                  const SautiAacSbrTables *tables,
                                  unsigned amp_res,
                                  unsigned balanced,
                                  SautiAacSbrEnergyFrame *out) {
    if (!quant || !grid || !out || amp_res > 1u ||
        balanced != 0u || /* paired CPE unmapping is not this stage */
        !valid_tables(tables) ||
        grid->frame_class > 3u ||
        grid->env_count < 1u ||
        grid->env_count > SAUTI_AAC_SBR_MAX_ENV ||
        grid->noise_count != (grid->env_count == 1u ? 1u : 2u) ||
        quant->env_count != grid->env_count ||
        quant->noise_count != grid->noise_count)
        return 0;

    for (unsigned e = 0; e < grid->env_count; ++e)
        if (grid->freq_res[e] > 1u ||
            quant->freq_res[e] != grid->freq_res[e]) return 0;

    SautiAacSbrEnergyFrame next;
    memset(&next, 0, sizeof(next));
    next.env_count = grid->env_count;
    next.noise_count = grid->noise_count;
    const unsigned fine =
        (grid->frame_class == 0u && grid->env_count == 1u) ||
        amp_res == 0u;

    /* The ISO SBR formulas are exactly representable in double except
     * for the sqrt(2) fine-resolution half-step. */
    const double half_step = 1.41421356237309504880168872420969808;
    for (unsigned e = 0; e < grid->env_count; ++e) {
        const unsigned res = grid->freq_res[e];
        const unsigned count = res ? tables->n_high : tables->n_low;
        next.freq_res[e] = (uint8_t)res;
        for (unsigned b = 0; b < count; ++b) {
            const unsigned q = quant->env[e][b];
            if (q > 127u) return 0;
            double val;
            if (fine)
                val = ldexp(1.0, 6 + (int)(q / 2u)) *
                      ((q & 1u) ? half_step : 1.0);
            else
                val = ldexp(1.0, 6 + (int)q);
            if (!isfinite(val) || val <= 0.0) return 0;
            next.env_energy[e][b] = val;
        }
    }

    for (unsigned e = 0; e < grid->noise_count; ++e) {
        for (unsigned b = 0; b < tables->n_noise; ++b) {
            const unsigned q = quant->noise[e][b];
            if (q > 30u) return 0;
            const double val = ldexp(1.0, 6 - (int)q);
            if (!isfinite(val) || val <= 0.0) return 0;
            next.noise_factor[e][b] = val;
        }
    }
    *out = next;
    return 1;
}
