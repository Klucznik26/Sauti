/* Native AAC SBR temporal grid (ISO/IEC 14496-3, sbr_grid).
 * No FFmpeg dependency or allocation. Uses the native NASM bit reader.
 * Only the classic HE-AAC SBR grid is supported, not USAC variants.
 * Decode to a local object and commit only after complete validation. */
#include "sauti_aac_sbr_grid.h"
#include "sauti_aac_headers.h"
#include <string.h>

static int get(const uint8_t *data, size_t size, size_t *pos,
               unsigned bits, uint32_t *value) {
    return sauti_aac_bits_read_asm(data, size, pos, bits, value);
}

/* The pointer has ceil(log2(env_count + 1)) bits, for 1..5 envelopes. */
static unsigned pointer_bits(unsigned env_count) {
    unsigned limit = env_count;
    unsigned bits = 0;
    while (limit) { ++bits; limit >>= 1; }
    return bits;
}

int sauti_aac_sbr_grid_parse(const uint8_t *data, size_t size,
                             size_t *bit_offset, unsigned time_slots,
                             SautiAacSbrGrid *out) {
    if (!data || !bit_offset || !out ||
        (time_slots != 15u && time_slots != 16u)) return 0;
    if (size > SIZE_MAX / 8u || *bit_offset > size * 8u) return 0;

    size_t pos = *bit_offset;
    uint32_t value = 0, leading_rel = 0, trailing_rel = 0;
    int border[SAUTI_AAC_SBR_MAX_ENV + 1u] = {0};
    SautiAacSbrGrid next;
    memset(&next, 0, sizeof(next));
    if (!get(data, size, &pos, 2, &next.frame_class)) return 0;

    if (next.frame_class == 0u) { /* FIXFIX */
        if (!get(data, size, &pos, 2, &value)) return 0;
        next.env_count = 1u << value;
        if (next.env_count > SAUTI_AAC_SBR_MAX_ENV ||
            !get(data, size, &pos, 1, &value)) return 0;
        for (unsigned i = 0; i < next.env_count; ++i)
            next.freq_res[i] = (uint8_t)value;
        border[0] = 0;
        border[next.env_count] = (int)time_slots;
        /* Rounded fixed envelope durations, as in the SBR grid. */
        int step = ((int)time_slots + (int)next.env_count / 2) /
                   (int)next.env_count;
        for (unsigned i = 1; i < next.env_count; ++i)
            border[i] = (int)i * step;
    } else { /* FIXVAR, VARFIX or VARVAR */
        if (next.frame_class == 1u) {
            border[0] = 0;
            if (!get(data, size, &pos, 2, &value)) return 0;
            /* Trailing absolute border may extend three time slots. */
            border[SAUTI_AAC_SBR_MAX_ENV] = (int)time_slots + (int)value;
            if (!get(data, size, &pos, 2, &trailing_rel)) return 0;
        } else if (next.frame_class == 2u) {
            if (!get(data, size, &pos, 2, &value)) return 0;
            border[0] = (int)value;
            if (!get(data, size, &pos, 2, &leading_rel)) return 0;
        } else {
            if (!get(data, size, &pos, 2, &value)) return 0;
            border[0] = (int)value;
            if (!get(data, size, &pos, 2, &value)) return 0;
            border[SAUTI_AAC_SBR_MAX_ENV] = (int)time_slots + (int)value;
            if (!get(data, size, &pos, 2, &leading_rel) ||
                !get(data, size, &pos, 2, &trailing_rel)) return 0;
        }

        next.env_count = leading_rel + trailing_rel + 1u;
        if (next.env_count > SAUTI_AAC_SBR_MAX_ENV) return 0;
        if (next.frame_class == 2u)
            border[next.env_count] = (int)time_slots;
        else
            border[next.env_count] = border[SAUTI_AAC_SBR_MAX_ENV];

        for (unsigned i = 0; i < leading_rel; ++i) {
            if (!get(data, size, &pos, 2, &value)) return 0;
            border[i + 1u] = border[i] + 2 * (int)value + 2;
        }
        for (unsigned i = 0; i < trailing_rel; ++i) {
            if (!get(data, size, &pos, 2, &value)) return 0;
            unsigned idx = next.env_count - i;
            border[idx - 1u] = border[idx] - 2 * (int)value - 2;
        }

        if (!get(data, size, &pos, pointer_bits(next.env_count),
                 &next.pointer) ||
            next.pointer > next.env_count + 1u) return 0;
        for (unsigned i = 0; i < next.env_count; ++i) {
            if (!get(data, size, &pos, 1, &value)) return 0;
            unsigned dst = next.frame_class == 1u
                           ? next.env_count - 1u - i : i;
            next.freq_res[dst] = (uint8_t)value;
        }
    }

    for (unsigned i = 0; i <= next.env_count; ++i) {
        if (border[i] < 0 || border[i] > (int)time_slots + 3 ||
            (i && border[i] <= border[i - 1u])) return 0;
        next.env_border[i] = (uint8_t)border[i];
    }

    next.noise_count = next.env_count > 1u ? 2u : 1u;
    next.noise_border[0] = next.env_border[0];
    next.noise_border[next.noise_count] =
        next.env_border[next.env_count];
    if (next.noise_count == 2u) {
        unsigned pivot = 0;
        if (next.frame_class == 0u) {
            pivot = next.env_count / 2u;
        } else if (next.frame_class & 1u) { /* FIXVAR or VARVAR */
            unsigned back = next.pointer > 1u ? next.pointer - 1u : 1u;
            if (back >= next.env_count) return 0;
            pivot = next.env_count - back;
        } else { /* VARFIX */
            pivot = next.pointer == 0u ? 1u
                : next.pointer == 1u ? next.env_count - 1u
                : next.pointer - 1u;
        }
        if (pivot == 0u || pivot >= next.env_count) return 0;
        next.noise_border[1] = next.env_border[pivot];
    }

    *out = next;
    *bit_offset = pos;
    return 1;
}
