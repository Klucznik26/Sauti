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
    return sauti_aac_sbr_grid_parse_max(data, size, bit_offset, time_slots, SAUTI_AAC_SBR_CLASSIC_MAX_ENV, out);
}
int sauti_aac_sbr_grid_parse_max(const uint8_t *data, size_t size,
                                 size_t *bit_offset, unsigned time_slots,
                                 unsigned max_env, SautiAacSbrGrid *out) {
    if (!data || !bit_offset || !out || max_env < 1u || max_env > SAUTI_AAC_SBR_MAX_ENV ||
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
        if (next.env_count > max_env ||
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
        if (next.env_count > max_env) return 0;
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

/* Low-delay grid tables (ISO/IEC 14496-3 Table 4.? / libxaac ixheaacd_ld_env_table_*, values only): {number of envelopes, border 1, border 2, transient envelope} per bs_transient_position */
static const int8_t ld_env_512[16][4] = { {2,4,-1,0}, {2,5,-1,0}, {3,2,6,1}, {3,3,7,1}, {3,4,8,1}, {3,5,9,1}, {3,6,10,1}, {3,7,11,1}, {3,8,12,1}, {3,9,13,1}, {3,10,14,1}, {2,11,-1,1}, {2,12,-1,1}, {2,13,-1,1}, {2,14,-1,1}, {2,15,-1,1} };
static const int8_t ld_env_480[15][4] = { {2,4,-1,0}, {2,5,-1,0}, {3,2,6,1}, {3,3,7,1}, {3,4,8,1}, {3,5,9,1}, {3,6,10,1}, {3,7,11,1}, {3,8,12,1}, {3,9,13,1}, {2,10,-1,1}, {2,11,-1,1}, {2,12,-1,1}, {2,13,-1,1}, {2,14,-1,1} };
int sauti_aac_sbr_grid_parse_ld(const uint8_t *data, size_t size, size_t *bit_offset, unsigned time_slots, unsigned *amp_res, SautiAacSbrGrid *out) {
    if (!data || !bit_offset || !out || !amp_res || (time_slots != 15u && time_slots != 16u)) return 0;
    if (size > SIZE_MAX / 8u || *bit_offset > size * 8u) return 0;
    size_t pos = *bit_offset;
    uint32_t cls = 0, v = 0;
    SautiAacSbrGrid next;
    memset(&next, 0, sizeof(next));
    next.ld = 1u;
    if (!get(data, size, &pos, 1, &cls)) return 0;
    if (cls == 0u) {                                                    /* FIXFIX */
        if (!get(data, size, &pos, 2, &v)) return 0;
        next.env_count = 1u << v;
        if (next.env_count > 2u) return 0;                               /* only one or two envelopes exist in a low-delay FIXFIX grid */
        if (next.env_count == 1u) { uint32_t a = 0; if (!get(data, size, &pos, 1, &a)) return 0; *amp_res = a; }
        if (!get(data, size, &pos, 1, &v)) return 0;
        for (unsigned i = 0; i < next.env_count; ++i) next.freq_res[i] = (uint8_t)v;
        next.frame_class = 0u;
        next.env_border[0] = 0;
        next.env_border[next.env_count] = (uint8_t)time_slots;
        if (next.env_count > 1u) {                                       /* relative borders of equal length: 8 for two envelopes, 5 for three (the table of the standard), reached only through bs_num_env = 2 or 4 here */
            static const int step[7] = { 8, 5, 0, 0, 0, 0, 0 };
            const unsigned lead = next.env_count - 1u;
            for (unsigned e = 1u; e <= lead; ++e) next.env_border[e] = (uint8_t)(e * (unsigned)step[lead - 1u]);
        }
        {
            const unsigned mid = next.env_count / 2u;
            next.noise_border[0] = next.env_border[0];
            if (next.env_count > 1u) { next.noise_count = 2u; next.noise_border[1] = next.env_border[mid]; next.noise_border[2] = next.env_border[next.env_count]; }
            else { next.noise_count = 1u; next.noise_border[1] = next.env_border[next.env_count]; }
        }
    } else {                                                            /* LD_TRAN */
        uint32_t tp = 0;
        const int8_t (*row)[4];
        if (!get(data, size, &pos, 4, &tp)) return 0;
        if (time_slots == 15u && tp >= 15u) return 0;
        row = time_slots == 16u ? &ld_env_512[tp] : &ld_env_480[tp];
        next.frame_class = 1u;                                            /* LD_TRAN (a one-envelope FIXFIX grid is the only frame_class 0 with a free amplitude resolution) */
        next.env_count = (unsigned)(*row)[0];
        for (unsigned i = 0; i < next.env_count; ++i) { if (!get(data, size, &pos, 1, &v)) return 0; next.freq_res[i] = (uint8_t)v; }
        next.env_border[0] = 0;
        next.env_border[next.env_count] = (uint8_t)time_slots;
        for (unsigned k = 1; k < next.env_count; ++k) next.env_border[k] = (uint8_t)(*row)[k];
        next.noise_border[0] = 0;
        next.noise_count = 2u; next.noise_border[1] = next.env_border[1]; next.noise_border[2] = (uint8_t)time_slots;   /* middle border = envelope border 1 (LD_TRAN: middle_bord = 1) */
        next.pointer = (unsigned)(*row)[3];                              /* the transient envelope index (the reference's SBR_ENVT_TRANIDX) */
    }
    *out = next;
    *bit_offset = pos;
    return 1;
}
