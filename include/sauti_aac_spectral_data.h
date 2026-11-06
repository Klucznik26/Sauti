#ifndef SAUTI_AAC_SPECTRAL_DATA_H
#define SAUTI_AAC_SPECTRAL_DATA_H
#include "sauti_aac_lc_sections.h"
#include <stdint.h>
#include <stddef.h>
/* Quantized AAC-LC spectral coefficients, before inverse quantization.
 * Short-window output uses natural eight-window layout [window][line].
 * PNS and intensity bands have no Huffman spectral data and remain zero.
 * On failure all 1024 output coefficients and *bit_offset are unchanged. */
int sauti_aac_lc_spectral_data_parse(const uint8_t *data,size_t length,
                                     size_t *bit_offset,
                                     unsigned sample_rate_index,
                                     const SautiAacIcsInfo *ics,
                                     const SautiAacSections *sections,
                                     int32_t output[1024]);
#endif
