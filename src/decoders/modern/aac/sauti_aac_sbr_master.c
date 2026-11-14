/* Classic HE-AAC SBR master frequency table, linear bs_freq_scale=0.
 * Integer arithmetic only, no external codec and no allocations.
 * This stage takes already computed k0/k2; start/stop frequency lookup
 * and logarithmic bs_freq_scale=1..3 are deliberately NOT implemented. */
#include "sauti_aac_sbr_master.h"
#include <string.h>

int sauti_aac_sbr_master_linear(unsigned k0, unsigned k2,
                                unsigned alter_scale, unsigned xover_band,
                                SautiAacSbrMaster *out) {
    if (!out || k0 == 0u || k0 >= k2 ||
        k2 > SAUTI_AAC_SBR_QMF_BANDS || alter_scale > 1u)
        return 0;

    const unsigned width = k2 - k0;
    const unsigned step = alter_scale + 1u;
    /* SBR: master count is even. Rounding depends on bs_alter_scale. */
    const unsigned count = ((width + (step & 2u)) >> step) << 1u;
    if (count < 2u || count > SAUTI_AAC_SBR_QMF_BANDS ||
        xover_band >= count)
        return 0;

    unsigned increments[SAUTI_AAC_SBR_QMF_BANDS] = {0};
    for (unsigned i = 0; i < count; ++i) increments[i] = step;

    const int remainder = (int)width - (int)(count * step);
    /* Residual is -2..+1. Distribute it at the ends of the grid,
     * preserving the strictly increasing master boundaries. */
    if (remainder == -1 || remainder == -2) {
        --increments[0];
        if (remainder == -2) --increments[1];
    } else if (remainder == 1) {
        ++increments[count - 1u];
    } else if (remainder != 0) {
        return 0;
    }

    SautiAacSbrMaster next;
    memset(&next, 0, sizeof(next));
    next.k0 = k0;
    next.k2 = k2;
    next.count = count;
    next.crossover_band = xover_band;
    unsigned edge = k0;
    next.edges[0] = (uint8_t)edge;
    for (unsigned i = 0; i < count; ++i) {
        if (increments[i] == 0u || edge + increments[i] > k2) return 0;
        edge += increments[i];
        next.edges[i + 1u] = (uint8_t)edge;
    }
    if (edge != k2) return 0;
    next.kx = next.edges[xover_band];
    *out = next;
    return 1;
}
