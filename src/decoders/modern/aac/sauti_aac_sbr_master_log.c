/* Classic MPEG-4 HE-AAC SBR master frequency table for logarithmic
 * scale modes 1..3. SBR-03: syntax-independent, no QMF and no FFmpeg.
 * The caller supplies previously validated k0/k2 for the SBR rate.
 * Zero allocation, copy-on-success, guarded against invalid borders. */
#include "sauti_aac_sbr_master.h"
#include <math.h>
#include <string.h>

#define SBR_MASTER_MAX_BANDS 48u

static void order_widths(int *widths, unsigned n) {
    for (unsigned i = 1; i < n; ++i) {
        int v = widths[i];
        unsigned j = i;
        while (j && widths[j - 1u] > v) {
            widths[j] = widths[j - 1u];
            --j;
        }
        widths[j] = v;
    }
}

/* Build increasing exponential breakpoints, then store their differences.
 * Sorting those differences later gives the SBR master distribution. */
static int geometric_widths(unsigned begin, unsigned end,
                            unsigned count, int widths[64]) {
    if (!begin || end <= begin || count < 2u ||
        count > SBR_MASTER_MAX_BANDS) return 0;
    const float factor = powf((float)end / (float)begin, 1.0f / (float)count);
    if (!isfinite(factor) || factor <= 1.0f) return 0;
    float value = (float)begin;
    int previous = (int)begin;
    for (unsigned i = 0; i + 1u < count; ++i) {
        value *= factor;
        if (!isfinite(value)) return 0;
        int nearest = (int)lrintf(value);
        widths[i] = nearest - previous;
        previous = nearest;
    }
    widths[count - 1u] = (int)end - previous;
    return 1;
}

static int round_even_band_count(unsigned begin, unsigned end,
                                 unsigned half_bands, float warping,
                                 unsigned *count) {
    if (!begin || end <= begin || !count) return 0;
    const float octaves = log2f((float)end / (float)begin);
    float estimate = (float)half_bands * warping * octaves;
    if (!isfinite(estimate) || estimate <= 0.0f) return 0;
    long rounded = lrintf(estimate);
    if (rounded <= 0 || rounded > (long)(SBR_MASTER_MAX_BANDS / 2u)) return 0;
    *count = (unsigned)rounded * 2u;
    return *count <= end - begin;
}

int sauti_aac_sbr_master_log(unsigned k0, unsigned k2,
                             unsigned freq_scale, unsigned alter_scale,
                             unsigned xover_band, SautiAacSbrMaster *out) {
    if (!out || !k0 || k0 >= k2 || k2 > SAUTI_AAC_SBR_QMF_BANDS ||
        k2 - k0 > 48u || freq_scale < 1u || freq_scale > 3u ||
        alter_scale > 1u || xover_band > 7u) return 0;

    const unsigned half_bands = 7u - freq_scale;
    /* The 2.2449 threshold selects two logarithmic regions. */
    const int divided = 49u * k2 > 110u * k0;
    const unsigned middle = divided ? 2u * k0 : k2;
    if (middle >= k2 && divided) return 0;

    unsigned left_count = 0, right_count = 0;
    if (!round_even_band_count(k0, middle, half_bands, 1.0f,
                               &left_count)) return 0;
    int left[64] = {0}, right[64] = {0};
    if (!geometric_widths(k0, middle, left_count, left)) return 0;
    order_widths(left, left_count);
    if (left[0] <= 0) return 0;

    if (divided) {
        const float warp = alter_scale ? 0.76923076923076923077f : 1.0f;
        if (!round_even_band_count(middle, k2, half_bands, warp,
                                   &right_count) ||
            left_count + right_count > SBR_MASTER_MAX_BANDS ||
            !geometric_widths(middle, k2, right_count, right)) return 0;
        order_widths(right, right_count);
        if (right[0] < left[left_count - 1u]) {
            const int mismatch = left[left_count - 1u] - right[0];
            const int room = (right[right_count - 1u] - right[0]) / 2;
            const int shift = mismatch < room ? mismatch : room;
            right[0] += shift;
            right[right_count - 1u] -= shift;
            order_widths(right, right_count);
        }
        if (right[0] <= 0) return 0;
    }

    unsigned total = left_count + right_count;
    if (total < 2u || total > SBR_MASTER_MAX_BANDS ||
        xover_band >= total) return 0;
    SautiAacSbrMaster next;
    memset(&next, 0, sizeof(next));
    next.k0 = k0;
    next.k2 = k2;
    next.count = total;
    next.crossover_band = xover_band;
    unsigned cursor = k0;
    next.edges[0] = (uint8_t)cursor;
    for (unsigned i = 0; i < total; ++i) {
        int step = i < left_count ? left[i] : right[i - left_count];
        if (step <= 0 || (unsigned)step > k2 - cursor) return 0;
        cursor += (unsigned)step;
        next.edges[i + 1u] = (uint8_t)cursor;
    }
    if (cursor != k2) return 0;
    next.kx = next.edges[xover_band];
    *out = next;
    return 1;
}
