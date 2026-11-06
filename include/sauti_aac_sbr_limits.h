#ifndef SAUTI_AAC_SBR_LIMITS_H
#define SAUTI_AAC_SBR_LIMITS_H

#include <stdint.h>

/* SBR-04: k0 and k2 in 64-channel QMF units from the OUTPUT SBR rate,
 * bs_start_freq and bs_stop_freq. This is header metadata only.
 * Frequencies unsupported by classic SBR fail closed. */
typedef struct {
    uint32_t sample_rate;
    uint32_t k0;
    uint32_t k2;
} SautiAacSbrLimits;

/* The sample_rate argument is SBR's output rate, not the AAC core rate.
 * No QMF, envelopes, or PCM reconstruction. Failure never modifies out. */
int sauti_aac_sbr_limits_compute(unsigned sample_rate, unsigned start_freq,
                                 unsigned stop_freq, SautiAacSbrLimits *out);
#endif
