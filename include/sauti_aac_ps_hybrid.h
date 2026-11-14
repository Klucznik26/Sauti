#ifndef SAUTI_AAC_PS_HYBRID_H
#define SAUTI_AAC_PS_HYBRID_H

#include "sauti_aac_ps_map.h"

/* A PS frame spans 32 QMF slots.  The hybrid filterbank exposes 71 bands in
 * the 20-parameter configuration and 91 bands in the 34-parameter one. */
#define SAUTI_AAC_PS_QMF_SLOTS 32u
#define SAUTI_AAC_PS_MAX_HYBRID_BANDS 91u

/* Immutable routing derived from a parsed and normalized PS frame.  It does
 * not contain PCM or filter state: hybrid analysis, decorrelation and matrix
 * mixing consume this schedule in later stages. */
typedef struct {
    uint32_t parameter_bands; /* 20 or 34 */
    uint32_t hybrid_bands;    /* 71 or 91 */
    uint32_t env_count;
    uint8_t slot_to_env[SAUTI_AAC_PS_QMF_SLOTS];
    uint8_t hybrid_to_parameter[SAUTI_AAC_PS_MAX_HYBRID_BANDS];
    int8_t phase_imaginary_sign[SAUTI_AAC_PS_MAX_HYBRID_BANDS];
} SautiAacPsHybridSchedule;

/* Builds the QMF-slot envelope routing and ISO PS hybrid-band routing
 * atomically.  Returns zero if the parsed and mapped views disagree or do
 * not cover every slot through 31. */
int sauti_aac_ps_hybrid_schedule_build(const SautiAacPsFrame *frame,
                                       const SautiAacPsMappedFrame *mapped,
                                       SautiAacPsHybridSchedule *out);

#endif
