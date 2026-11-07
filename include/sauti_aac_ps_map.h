#ifndef SAUTI_AAC_PS_MAP_H
#define SAUTI_AAC_PS_MAP_H

#include "sauti_aac_syntax.h"

/* PS parameter-band normalization before hybrid-QMF processing. The mixer
 * operates in either 20- or 34-band mode; IID/ICC select the mode, while
 * IPD/OPD retain their 5/11/17-band semantics within that same layout. */
typedef struct {
    uint32_t bands;       /* 20 or 34 */
    uint32_t env_count;   /* resolved PS envelopes, 1..5 */
    int8_t iid[SAUTI_AAC_PS_MAX_ENVELOPES][SAUTI_AAC_PS_MAX_PARAMETERS];
    int8_t icc[SAUTI_AAC_PS_MAX_ENVELOPES][SAUTI_AAC_PS_MAX_PARAMETERS];
    int8_t ipd[SAUTI_AAC_PS_MAX_ENVELOPES][SAUTI_AAC_PS_MAX_PARAMETERS];
    int8_t opd[SAUTI_AAC_PS_MAX_ENVELOPES][SAUTI_AAC_PS_MAX_PARAMETERS];
} SautiAacPsMappedFrame;

/* Normalizes every resolved envelope atomically. No PCM/QMF state changes. */
int sauti_aac_ps_map_frame(const SautiAacPsFrame *frame,
                           SautiAacPsMappedFrame *out);
#endif
