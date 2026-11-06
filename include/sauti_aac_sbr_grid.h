#ifndef SAUTI_AAC_SBR_GRID_H
#define SAUTI_AAC_SBR_GRID_H

#include <stddef.h>
#include <stdint.h>

/* HE-AAC v1 (MPEG-4 SBR), Table 4.69 sbr_grid() time grid.
 * This parses and validates only the time/frequency-resolution grid,
 * not SBR Huffman envelopes, QMF, high-frequency synthesis or PCM.
 * Classic AAC SBR supports at most five envelopes (USAC excluded). */
#define SAUTI_AAC_SBR_MAX_ENV 5u

typedef struct {
    uint32_t frame_class;           /* 0 FIXFIX, 1 FIXVAR, 2 VARFIX, 3 VARVAR */
    uint32_t env_count;             /* 1..5 */
    uint32_t noise_count;           /* 1 or 2 */
    uint32_t pointer;               /* bs_pointer, zero in FIXFIX */
    uint8_t freq_res[SAUTI_AAC_SBR_MAX_ENV];
    uint8_t env_border[SAUTI_AAC_SBR_MAX_ENV + 1u];
    uint8_t noise_border[3];
} SautiAacSbrGrid;

/* time_slots=16 for 1024-sample AAC, or 15 for 960-sample AAC.
 * Bit offset may be unaligned. A failed parse leaves *bit_offset and
 * *out entirely unchanged, including on truncation/invalid borders. */
int sauti_aac_sbr_grid_parse(const uint8_t *data, size_t size,
                             size_t *bit_offset, unsigned time_slots,
                             SautiAacSbrGrid *out);
#endif
