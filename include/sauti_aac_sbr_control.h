#ifndef SAUTI_AAC_SBR_CONTROL_H
#define SAUTI_AAC_SBR_CONTROL_H

#include "sauti_aac_sbr_grid.h"
#include "sauti_aac_sbr_tables.h"
#include <stddef.h>
#include <stdint.h>

/* HE-AAC v1 SBR-06: control bits following sbr_grid().
 * Per classic MPEG-4 syntax, sbr_dtdf() contains one 1-bit flag per
 * envelope and per noise envelope, followed by sbr_invf() containing
 * one 2-bit inverse-filtering mode per noise FREQUENCY band.
 * This is not Huffman envelope/noise decoding or QMF synthesis. */
typedef struct {
    uint32_t env_count;                    /* 1..5, from SBR time grid */
    uint32_t noise_count;                  /* 1 or 2, from time grid */
    uint32_t noise_band_count;             /* 1..5, from SBR frequency table */
    uint8_t df_env[SAUTI_AAC_SBR_MAX_ENV];
    uint8_t df_noise[2];
    uint8_t invf_mode[SAUTI_AAC_SBR_MAX_NOISE_BANDS]; /* 0..3 */
} SautiAacSbrControl;

/* Parse consecutive classic SBR sbr_dtdf() and sbr_invf() elements.
 * The caller must already have parsed the grid and frequency tables.
 * Bit offset may be non-byte-aligned. Failure leaves *bit_offset and
 * *out byte-for-byte unchanged. No allocation or external decoder. */
int sauti_aac_sbr_control_parse(const uint8_t *data, size_t size,
                                 size_t *bit_offset,
                                 const SautiAacSbrGrid *grid,
                                 const SautiAacSbrTables *tables,
                                 SautiAacSbrControl *out);
#endif
