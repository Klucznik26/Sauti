/* Independent HE-AAC SBR-04: MPEG-4 start/stop frequency parameters.
 * Standards-derived integer offset tables, no external codec.
 * Only ordinary double-rate MPEG-4 SBR output sampling rates.
 * No state mutation on unsupported/invalid data. */
#include "sauti_aac_sbr_limits.h"
#include <math.h>
#include <stdint.h>

/* Indices 0..15 are the four-bit bs_start_freq value. */
static const int8_t start_offset[6][16] = {
    {-8,-7,-6,-5,-4,-3,-2,-1, 0, 1, 2, 3, 4, 5, 6, 7},
    {-5,-4,-3,-2,-1, 0, 1, 2, 3, 4, 5, 6, 7, 9,11,13},
    {-5,-3,-2,-1, 0, 1, 2, 3, 4, 5, 6, 7, 9,11,13,16},
    {-6,-4,-2,-1, 0, 1, 2, 3, 4, 5, 6, 7, 9,11,13,16},
    {-4,-2,-1, 0, 1, 2, 3, 4, 5, 6, 7, 9,11,13,16,20},
    {-2,-1, 0, 1, 2, 3, 4, 5, 6, 7, 9,11,13,16,20,24}
};

static int sample_rate_group(unsigned sample_rate) {
    switch (sample_rate) {
        case 16000: return 0;
        case 22050: return 1;
        case 24000: return 2;
        case 32000: return 3;
        case 44100: case 48000: case 64000: return 4;
        case 88200: case 96000: case 128000:
        case 176400: case 192000: return 5;
        default: return -1;
    }
}

static void sorted_increments(unsigned minimum, int step[13]) {
    float val = (float)minimum;
    const float mult = powf(64.0f / (float)minimum, 1.0f / 13.0f);
    int previous = (int)minimum;
    for (unsigned i=0; i<12; ++i) {
        val *= mult;
        const int current = (int)lrintf(val);
        step[i] = current - previous;
        previous = current;
    }
    step[12] = 64 - previous;
    /* Insertion sort 13 positive increments, reproducible without qsort. */
    for (unsigned i=1; i<13; ++i) {
        int value = step[i];
        unsigned j=i;
        while (j && step[j-1] > value) {
            step[j] = step[j-1];
            --j;
        }
        step[j] = value;
    }
}

int sauti_aac_sbr_limits_compute(unsigned sample_rate, unsigned start_freq,
                                 unsigned stop_freq, SautiAacSbrLimits *out) {
    if (!out || start_freq > 15u || stop_freq > 15u) return 0;
    const int group = sample_rate_group(sample_rate);
    if (group < 0) return 0;
    const unsigned base_hz = sample_rate < 32000u ? 3000u :
                             sample_rate < 64000u ? 4000u : 5000u;
    const int start_min = (int)((base_hz * 128u + sample_rate/2u)/sample_rate);
    const unsigned stop_min = (base_hz * 256u + sample_rate/2u)/sample_rate;
    const int k0 = start_min + start_offset[group][start_freq];
    if (k0 < 1 || k0 > 63 || stop_min < 1u || stop_min >= 64u) return 0;

    unsigned k2;
    if (stop_freq <= 13u) {
        int widths[13];
        sorted_increments(stop_min,widths);
        k2 = stop_min;
        for (unsigned i=0; i<stop_freq; ++i) {
            if (widths[i] <= 0) return 0;
            k2 += (unsigned)widths[i];
        }
    } else {
        k2 = (unsigned)k0 * (stop_freq == 14u ? 2u : 3u);
    }
    if (k2 > 64u) k2 = 64u;
    const unsigned max_width = sample_rate <= 32000u ? 48u :
                               sample_rate == 44100u ? 35u : 32u;
    if (k2 <= (unsigned)k0 || k2 - (unsigned)k0 > max_width) return 0;
    SautiAacSbrLimits next = {sample_rate, (uint32_t)k0, k2};
    *out = next;
    return 1;
}
