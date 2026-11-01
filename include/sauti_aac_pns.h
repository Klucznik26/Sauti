#ifndef SAUTI_AAC_PNS_H
#define SAUTI_AAC_PNS_H
#include "sauti_aac_scalefactors.h"
#include "sauti_aac_syntax.h"
#include <stdint.h>
/* AAC-19: deterministic mono PNS spectral injection after dequant.
 * 13=NOISE_HCB; zero/spectral bands are untouched; intensity is rejected.
 * The seed advances only on success; energy per noise band is normalized.
 * State is for reproducibility, not a claim of sample-exact FFmpeg RNG. */
int sauti_aac_pns_apply(const SautiAacIcsInfo *ics,
                        unsigned sample_rate_index,
                        const SautiAacSections *sections,
                        const SautiAacScaleFactors *factors,
                        uint32_t *seed,float spectral[1024]);
#endif
