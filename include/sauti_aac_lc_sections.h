#ifndef SAUTI_AAC_LC_SECTIONS_H
#define SAUTI_AAC_LC_SECTIONS_H
#include "sauti_aac_syntax.h"
#include <stddef.h>
#include <stdint.h>

/* Structural AAC-LC section_data() for non-error-resilient streams.
 * Each group must cover the range [0, max_sfb) exactly once.
 * Huffman coefficient decoding and scale-factor decoding follow later.
 */
#define SAUTI_AAC_MAX_SECTIONS 128u
typedef struct {
    uint8_t group;       /* 0..7 */
    uint8_t codebook;    /* 0..15 except reserved 12 */
    uint8_t first_sfb;   /* inclusive */
    uint8_t end_sfb;     /* exclusive */
} SautiAacSection;

typedef struct {
    uint32_t section_count;
    uint32_t group_count;
    uint32_t max_sfb;
    SautiAacSection sections[SAUTI_AAC_MAX_SECTIONS];
} SautiAacSections;

/* On failure neither *bit_offset nor *sections changes. */
int sauti_aac_lc_sections_parse(const uint8_t *data, size_t length,
                                size_t *bit_offset,
                                const SautiAacIcsInfo *ics,
                                SautiAacSections *sections);

/* Reads the beginning of raw_data_block() ID_SCE only (single channel):
 * element_instance_tag, global_gain, ics_info(), section_data().
 * Returns at the beginning of scale_factor_data(); NOT a full AAC decode.
 * Caller must skip preceding valid FIL via sauti_aac_raw_prefix_parse().
 * No CPE / stereo channel pair parsing yet.
 */
typedef struct {
    uint32_t instance_tag;
    uint32_t global_gain;
    SautiAacIcsInfo ics;
    SautiAacSections sections;
} SautiAacScePrefix;

int sauti_aac_lc_sce_prefix_parse(const uint8_t *data, size_t length,
                                  size_t *bit_offset,
                                  SautiAacScePrefix *out);
#endif
