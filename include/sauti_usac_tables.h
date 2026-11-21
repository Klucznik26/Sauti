#ifndef SAUTI_USAC_TABLES_H
#define SAUTI_USAC_TABLES_H
#include <stdint.h>
/* USAC constant tables, see src/decoders/modern/aac/sauti_usac_tables.c (generated, LGPL-derived data). */
extern const uint16_t sauti_usac_ac_msb_cdf[64][17];       /* cumulative frequencies of the 2-tuple MSB symbols (16 = escape), 14-bit */
extern const uint16_t sauti_usac_ac_lsb_cdf[3][4];         /* residual bit pairs: [0] a != 0 && b == 0 ... index = !a ? 1 : (!b ? 0 : 2) */
extern const uint8_t sauti_usac_ac_lookup[742];            /* context -> probability model, fallback by binary search */
extern const uint32_t sauti_usac_ac_hash[742];             /* (context << 8) | model, sorted by context */
extern const uint16_t *const sauti_usac_swb768[13];        /* band offsets of 768-line long windows per sampling_frequency_index */
extern const uint16_t *const sauti_usac_swb96[13];         /* band offsets of 96-line short windows (768-line frames) */
extern const uint8_t sauti_usac_num_swb768[13], sauti_usac_num_swb96[13];
extern const uint8_t sauti_usac_tns_max_bands_1024[13], sauti_usac_tns_max_bands_128[13];
extern const uint8_t sauti_usac_noise_fill_start[4];       /* [frame_length_short][short window] */
extern const int sauti_usac_samplerate[32];                /* usacSamplingFrequencyIndex -> Hz, -1 = reserved */
extern const float sauti_usac_mdst_filt_cur[4][4][7];      /* complex prediction: [window sequence][window shape pair][taps] */
#endif
