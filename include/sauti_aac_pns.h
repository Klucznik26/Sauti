#ifndef SAUTI_AAC_PNS_H
#define SAUTI_AAC_PNS_H
#include "sauti_aac_scalefactors.h"
#include "sauti_aac_syntax.h"
#include <stdint.h>
/* AAC-19/29B: deterministic PNS spectral injection after dequant.
 * 13=NOISE_HCB; zero/spectral/intensity bands are untouched.
 * Every noise SFB in every window is normalized to its transmitted gain.
 * Caller supplies the state of the shared decoder RNG stream; the state
 * advances only on success and is otherwise unchanged. The resulting
 * samples need not be identical across standards-conformant decoders. */
int sauti_aac_pns_apply(const SautiAacIcsInfo *ics,
                        unsigned sample_rate_index,
                        const SautiAacSections *sections,
                        const SautiAacScaleFactors *factors,
                        uint32_t *seed,float spectral[1024]);
#endif
