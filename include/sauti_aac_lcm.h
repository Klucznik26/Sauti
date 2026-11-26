#ifndef SAUTI_AAC_LCM_H
#define SAUTI_AAC_LCM_H
#include "sauti_aac_lcn.h"
#include "sauti_aac_lc_multi.h"

/* AAC Main and AAC LTP with more than two channels (channel_configuration 3..7: 3.0, 4.0, 5.0, 5.1, 7.1), the multichannel counterpart of sauti_aac_lcn.h: raw_data_block with SCE / CPE / LFE
 * elements in stream order (configuration 6: C, L R, Ls Rs, LFE), DSE and non-SBR FIL skipped, one noise generator for the whole frame, the channels returned in the WAVE / FFmpeg output order
 * of sauti_aac_lc_multi_output_map.  Per-channel predictors (Main) or time buffers (LTP) make the state large (about 230 KiB), so it is a separate type that callers allocate.  Not supported:
 * channel_configuration 0 (program_config_element), CCE, ADTS error check, SBR. */
typedef struct {
    unsigned channels;                          /* 3..8 */
    unsigned config;                            /* channel_configuration 3..7 */
    SautiAacLcSynthesis syn[SAUTI_AAC_LC_MULTI_MAX];
    uint32_t pns_seed;
    union { SautiAacPredState pred[SAUTI_AAC_LC_MULTI_MAX]; SautiAacLtpState ltp[SAUTI_AAC_LC_MULTI_MAX]; SautiAacSsrState ssr[SAUTI_AAC_LC_MULTI_MAX]; } u;     /* Main XOR LTP XOR SSR */
} SautiAacLcmState;

/* The number of channels of a supported configuration (cfg->channel_config 3..7, cfg->main xor cfg->ltp, the rate rules of sauti_aac_lcn_frame_samples), 0 otherwise. */
unsigned sauti_aac_lcm_channels(const SautiAacLcnConfig *config);
void sauti_aac_lcm_state_init(SautiAacLcmState *state,const SautiAacLcnConfig *config);
/* Atomic: on failure neither the state nor `pcm` (1024 frames x channels, interleaved, output order) changes.  1 decoded, 0 malformed / truncated, -1 valid but unsupported. */
int sauti_aac_lcm_decode(const SautiAacLcnConfig *config,SautiAacLcmState *state,const uint8_t *au,size_t length,float *pcm,SautiAacReason *reason);
#endif
