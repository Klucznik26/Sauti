#ifndef SAUTI_AAC_SBR_GRID_H
#define SAUTI_AAC_SBR_GRID_H

#include <stddef.h>
#include <stdint.h>

/* HE-AAC v1 (MPEG-4 SBR), Table 4.69 sbr_grid() time grid.
 * This parses and validates only the time/frequency-resolution grid,
 * not SBR Huffman envelopes, QMF, high-frequency synthesis or PCM.
 * Classic AAC SBR supports at most five envelopes; the grid of USAC (eSBR) up to eight (FIXFIX with 1 << 3 envelopes). */
#define SAUTI_AAC_SBR_CLASSIC_MAX_ENV 5u
#define SAUTI_AAC_SBR_MAX_ENV 8u

typedef struct {
    uint32_t frame_class;           /* 0 FIXFIX, 1 FIXVAR, 2 VARFIX, 3 VARVAR */
    uint32_t env_count;             /* 1..5 */
    uint32_t noise_count;           /* 1 or 2 */
    uint32_t pointer;               /* bs_pointer, zero in FIXFIX */
    uint8_t freq_res[SAUTI_AAC_SBR_MAX_ENV];
    uint8_t env_border[SAUTI_AAC_SBR_MAX_ENV + 1u];
    uint8_t noise_border[3];
    uint32_t ld;                    /* 1: low-delay grid (AAC-LD / ELD with SBR): borders in QMF slots, and bs_amp_res of a one-envelope FIXFIX grid is transmitted instead of being 0 */
} SautiAacSbrGrid;

/* time_slots=16 for 1024-sample AAC, or 15 for 960-sample AAC.
 * Bit offset may be unaligned. A failed parse leaves *bit_offset and
 * *out entirely unchanged, including on truncation/invalid borders. */
int sauti_aac_sbr_grid_parse(const uint8_t *data, size_t size,
                             size_t *bit_offset, unsigned time_slots,
                             SautiAacSbrGrid *out);
/* The same with the largest accepted envelope count chosen by the caller (5 classic, 8 for USAC). */
/* sbr_grid() of AAC-LD / AAC-ELD (ISO/IEC 14496-3 4.6.18, low delay): bs_frame_class has ONE bit (0 FIXFIX, 1 LD_TRAN); `time_slots` 16 (frame 512) or 15 (480); *amp_res receives the transmitted bs_amp_res of
 * a one-envelope FIXFIX grid (else untouched), `grid->ld` is set. */
int sauti_aac_sbr_grid_parse_ld(const uint8_t *data, size_t size, size_t *bit_offset, unsigned time_slots, unsigned *amp_res, SautiAacSbrGrid *out);
int sauti_aac_sbr_grid_parse_max(const uint8_t *data, size_t size,
                                 size_t *bit_offset, unsigned time_slots,
                                 unsigned max_env, SautiAacSbrGrid *out);
#endif
