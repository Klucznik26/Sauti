#ifndef SAUTI_AAC_SCALEFACTORS_H
#define SAUTI_AAC_SCALEFACTORS_H
#include "sauti_aac_lc_sections.h"
#include <stddef.h>
#include <stdint.h>

#define SAUTI_AAC_SF_GROUPS 8u
#define SAUTI_AAC_SF_BANDS 64u
/* Values are raw cumulative AAC scale-factor, intensity-position and
 * noise-energy indices, not final frequency-domain gain multipliers. */
typedef struct {
    int32_t value[SAUTI_AAC_SF_GROUPS][SAUTI_AAC_SF_BANDS];
    uint8_t codebook[SAUTI_AAC_SF_GROUPS][SAUTI_AAC_SF_BANDS];
} SautiAacScaleFactors;

/* Consume scale_factor_data() starting immediately after section_data().
 * No heap, no FFmpeg dependency. Validates coverage and codebooks.
 * On failure both output and bit_offset remain unchanged.
 * CODEBOOK 0: zero; 1..11: spectral factors; 13: noise (first uses
 * 9-bit PCM offset), 14/15: intensity positions. */
int sauti_aac_lc_scalefactors_parse(const uint8_t *data,size_t length,
                                   size_t *bit_offset,uint32_t global_gain,
                                   const SautiAacSections *sections,
                                   SautiAacScaleFactors *out);
#endif
