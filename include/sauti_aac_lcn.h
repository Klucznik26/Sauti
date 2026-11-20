#ifndef SAUTI_AAC_LCN_H
#define SAUTI_AAC_LCN_H
#include "sauti_aac_cpe.h"
#include "sauti_aac_layout.h"
#include <stddef.h>
#include <stdint.h>

/* AAC-LC with frameLengthFlag = 1 (GASpecificConfig) and AAC Main: raw_data_block decoders without ADTS framing.  LC 960: 960 lines per frame and eight short windows of 120 (DAB+, DRM, some broadcast chains).  The access unit is an ordinary
 * raw_data_block (element ids, FIL, DSE, ID_END), but it cannot be written in ADTS (ADTS has no frame-length flag), so containers hand it over without a header together with the
 * AudioSpecificConfig.  Mono and stereo (channel_configuration 1 and 2); SBR (HE-AAC with 960) is not supported and is refused. */
typedef struct {
    unsigned rate_index;       /* sampling_frequency_index of the core, 0..12 (0..11 for Main) */
    unsigned channel_config;   /* 1 or 2 */
    unsigned main;             /* 1: AAC Main (object type 1): 1024 lines per frame, ics_info predictor_data, backward-adaptive prediction (sauti_aac_pred.h); 0: AAC-LC with 960 lines */
} SautiAacLcnConfig;
typedef struct {
    SautiAacLcSynthesis channel[2];
    SautiAacPredState pred[2];    /* Main only */
} SautiAacLcnState;

unsigned sauti_aac_lcn_frame_samples(const SautiAacLcnConfig *config);       /* 960 (LC) or 1024 (Main), or 0 for an unsupported configuration */
void sauti_aac_lcn_state_init(SautiAacLcnState *state);
/* Decodes one access unit to interleaved float PCM (channels x 960).  Atomic: on failure state and `pcm` are unchanged.  1 decoded, 0 malformed / truncated, -1 valid but unsupported
 * (SBR in a fill element, LFE / CCE / PCE elements, gain control, intensity outside a common-window pair). */
int sauti_aac_lcn_decode(const SautiAacLcnConfig *config,SautiAacLcnState *state,const uint8_t *au,size_t length,float *pcm,SautiAacReason *reason);
#endif
