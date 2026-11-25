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
/* The same, but the bands flagged in `skip[group][band]` (NULL = none) get no noise and do not advance the generator: the second channel of a pair, bands with correlated PNS
 * (both channels NOISE_HCB and ms_used): their noise is a copy of the first channel's (sauti_aac_cpe.c), and FDK does not consume random numbers for them. */
int sauti_aac_pns_apply_skip(const SautiAacIcsInfo *ics,
                        unsigned sample_rate_index,
                        const SautiAacSections *sections,
                        const SautiAacScaleFactors *factors,
                        uint32_t *seed,const uint8_t skip[8][64],float spectral[1024]);
#endif
