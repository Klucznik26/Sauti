#ifndef SAUTI_AAC_DEQUANT_H
#define SAUTI_AAC_DEQUANT_H
#include "sauti_aac_scalefactors.h"
#include <stdint.h>
#include <stddef.h>

/* AAC-LC: sign(q) * abs(q)^(4/3) * 2^((scalefactor - 100)/4).
 * PNS, ZERO and intensity bands remain zero: those need separate processing.
 * Quantized input uses natural [window][line] layout from spectral_data.
 * No modifications to output on error. */
int sauti_aac_lc_dequantize(const int32_t quant[1024],
                            unsigned sample_rate_index,
                            const SautiAacIcsInfo *ics,
                            const SautiAacSections *sections,
                            const SautiAacScaleFactors *factors,
                            float output[1024]);

/* NASM SSE kernel: signed magnitude and scale for count samples.
 * q and positive_pow43 must be nonoverlapping with dst. */
int sauti_aac_dequant_scale_asm(const int32_t *q,
                               const float *positive_pow43,
                               const float *scale,
                               float *dst,size_t count);
#endif
