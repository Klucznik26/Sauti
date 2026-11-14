/* HE-AAC v1 SBR-05: derive high-, low- and noise-resolution QMF edges.
 * Integer grouping, with libm only for the spec-defined noise band count.
 * No FFmpeg or other external decoder, allocations or PCM synthesis. */
#include "sauti_aac_sbr_tables.h"
#include <math.h>
#include <string.h>

int sauti_aac_sbr_tables_build(const SautiAacSbrMaster *master,
                                unsigned noise_bands, SautiAacSbrTables *out) {
    if (!master || !out || noise_bands > 3u ||
        master->count < 2u ||
        master->count > SAUTI_AAC_SBR_MAX_MASTER_BANDS ||
        (master->count & 1u) ||
        master->k0 < 1u || master->k0 >= master->k2 ||
        master->k2 > SAUTI_AAC_SBR_QMF_BANDS ||
        master->crossover_band > 7u ||
        master->crossover_band >= master->count ||
        master->edges[0] != master->k0 ||
        master->edges[master->count] != master->k2 ||
        master->kx != master->edges[master->crossover_band]) return 0;

    for (unsigned i = 1u; i <= master->count; ++i)
        if (master->edges[i] <= master->edges[i - 1u]) return 0;

    const unsigned n_high = master->count - master->crossover_band;
    const unsigned n_low = (n_high + 1u) / 2u;
    const unsigned kx = master->kx;
    if (kx < 1u || kx > 32u || n_high == 0u || n_low == 0u ||
        n_low > SAUTI_AAC_SBR_MAX_MASTER_BANDS / 2u) return 0;

    const float estimate = (float)noise_bands *
                           log2f((float)master->k2 / (float)kx);
    if (!isfinite(estimate)) return 0;
    long rounded = lrintf(estimate);
    if (rounded < 1L) rounded = 1L;
    /* Values beyond five noise floors are invalid; do not clamp to five.
     * Also refuse repeated/zero-width noise groups when n_low is small. */
    if (rounded > (long)SAUTI_AAC_SBR_MAX_NOISE_BANDS ||
        rounded > (long)n_low) return 0;

    SautiAacSbrTables next;
    memset(&next, 0, sizeof(next));
    next.kx = kx;
    next.m = master->k2 - kx;
    next.n_high = n_high;
    next.n_low = n_low;
    next.n_noise = (unsigned)rounded;
    for (unsigned i = 0; i <= n_high; ++i)
        next.high[i] = master->edges[master->crossover_band + i];

    next.low[0] = next.high[0];
    const unsigned odd = n_high & 1u;
    for (unsigned i = 1; i <= n_low; ++i)
        next.low[i] = next.high[2u * i - odd];

    next.noise[0] = next.low[0];
    unsigned idx = 0u;
    for (unsigned i = 1u; i <= next.n_noise; ++i) {
        idx += (n_low - idx) / (next.n_noise + 1u - i);
        if (idx > n_low ||
            next.low[idx] <= next.noise[i - 1u]) return 0;
        next.noise[i] = next.low[idx];
    }
    if (next.noise[next.n_noise] != master->k2 ||
        next.low[n_low] != master->k2) return 0;
    *out = next;
    return 1;
}
