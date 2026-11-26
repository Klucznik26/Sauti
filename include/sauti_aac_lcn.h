#ifndef SAUTI_AAC_LCN_H
#define SAUTI_AAC_LCN_H
#include "sauti_aac_cpe.h"
#include "sauti_aac_layout.h"
#include "sauti_aac_ltp.h"
#include <stddef.h>
#include <stdint.h>

/* AAC-LC with frameLengthFlag = 1 (GASpecificConfig) and AAC Main: raw_data_block decoders without ADTS framing.  LC 960: 960 lines per frame and eight short windows of 120 (DAB+, DRM, some broadcast chains).  The access unit is an ordinary
 * raw_data_block (element ids, FIL, DSE, ID_END), but it cannot be written in ADTS (ADTS has no frame-length flag), so containers hand it over without a header together with the
 * AudioSpecificConfig.  Mono and stereo (channel_configuration 1 and 2); SBR (HE-AAC with 960) is not supported and is refused. */
typedef struct {
    unsigned rate_index;       /* sampling_frequency_index of the core, 0..12 (0..11 for Main) */
    unsigned channel_config;   /* 1 or 2 */
    unsigned er;               /* 1: error-resilient syntax without resilience tools (object types 17 ER AAC LC and 19 ER AAC LTP with `ltp`): no element ids / ID_END, see SAUTI_AAC_IDX_ER_FLAG; `frame960` selects 960 lines (LC only) */
    unsigned ltp;              /* 1: AAC LTP (object type 4): 1024 lines, ics_info ltp_data, long-term prediction from the decoder's own output (sauti_aac_ltp.h); exclusive with `main` */
    unsigned frame960;         /* with `main`: frameLengthFlag = 1, 960 lines per frame (the predictor works on the first 960 states); not defined for LTP */
    unsigned ssr;              /* 1: AAC SSR (object type 3): 1024 lines, gain control tool and the 4-band filter bank (sauti_aac_ssr.h); exclusive with main / ltp / er */
    unsigned main;             /* 1: AAC Main (object type 1): 1024 lines per frame, ics_info predictor_data, backward-adaptive prediction (sauti_aac_pred.h); 0: AAC-LC with 960 lines */
} SautiAacLcnConfig;
typedef struct {
    SautiAacLcSynthesis channel[2];
    SautiAacPredState pred[2];    /* Main only */
    SautiAacLtpState ltp[2];      /* LTP only */
    SautiAacSsrState ssr[2];      /* SSR only */
} SautiAacLcnState;

unsigned sauti_aac_lcn_frame_samples(const SautiAacLcnConfig *config);       /* 960 (LC) or 1024 (Main, LTP), or 0 for an unsupported configuration */
void sauti_aac_lcn_state_init(SautiAacLcnState *state);
/* Decodes one access unit to interleaved float PCM (channels x 960).  Atomic: on failure state and `pcm` are unchanged.  1 decoded, 0 malformed / truncated, -1 valid but unsupported
 * (SBR in a fill element, LFE / CCE / PCE elements, gain control, intensity outside a common-window pair). */
int sauti_aac_lcn_decode(const SautiAacLcnConfig *config,SautiAacLcnState *state,const uint8_t *au,size_t length,float *pcm,SautiAacReason *reason);
/* The same for an access unit carried in an ADTS frame WITH error check (protection_absent = 0): `adts_header` points at the 7 header bytes, `crc` is the 16-bit value from the frame; the
 * CRC-16 over the header and the protected bits of the channel element (the first 192 bits after its id, plus for a CPE the first 128 bits of the second channel; zero-padded when shorter)
 * must match, otherwise 0 (malformed) and nothing changes. */
int sauti_aac_lcn_decode_adts(const SautiAacLcnConfig *config,SautiAacLcnState *state,const uint8_t *au,size_t length,float *pcm,SautiAacReason *reason,const uint8_t adts_header[7],unsigned crc);
/* The same for HE-AAC with 960 (DAB+): the SBR extension payload of the first SBR fill element (type 13 / 14) is not refused but reported (`bits` includes the 4-bit type nibble, as in
 * SautiAacFillInfo); `sbr->present` = 0 when the access unit carries none.  The core PCM is the plain AAC-LC core signal at the core rate. */
typedef struct { unsigned present, type; size_t start_bit, bits; } SautiAacLcnSbr;
int sauti_aac_lcn_decode_sbr(const SautiAacLcnConfig *config,SautiAacLcnState *state,const uint8_t *au,size_t length,float *pcm,SautiAacReason *reason,SautiAacLcnSbr *sbr);
/* Element level (shared with the multichannel decoder sauti_aac_lcm.h): the state pointers of one channel (pred / ltp may be NULL when the profile does not use them). */
typedef struct { SautiAacLcSynthesis *syn; SautiAacPredState *pred; SautiAacLtpState *ltp; SautiAacSsrState *ssr; } SautiAacLcnChannel;
unsigned sauti_aac_lcn_index(const SautiAacLcnConfig *config);       /* the packed index (rate + layout + profile flag) of a configuration */
/* One channel element (id 0 SCE, 1 CPE, 3 LFE) that starts at bit `at` (its id included): parse, dequantise, PNS / M/S / intensity, prediction, synthesis, LTP buffer update.  The channel
 * states and *pns_seed advance in place (callers work on copies and drop them on failure); *pos = end of the element; out[0] (out[1] for a CPE) = output in decoder units.  1 / 0 / -1.
 * adts_header / adts_crc as in sauti_aac_lcn_decode_adts (NULL = no check). */
int sauti_aac_lcn_element(const SautiAacLcnConfig *config,unsigned idx,unsigned id,const uint8_t *au,size_t length,size_t at,size_t *pos,
                          SautiAacLcnChannel ch[2],uint32_t *pns_seed,float out[2][1024],SautiAacReason *reason,const uint8_t *adts_header,unsigned adts_crc);
/* Staged decoding for the wide multichannel decoder (sauti_aac_lcw.h): plain LC (1024 / 960 lines) and Main only.  `stage_element` parses an SCE / CPE / LFE and leaves the spectra before TNS (after
 * dequantisation, PNS, M/S, intensity, Main prediction); `stage_cce` does that for the individual_channel_stream of a coupling channel element; `stage_tns` applies the TNS of a stage; `stage_synth`
 * runs the filter bank (window, IMDCT, overlap-add) on spectra whose TNS has been applied.  1 / 0 / -1 as sauti_aac_lcn_element. */
typedef struct { SautiAacIcsInfo ics; SautiAacTnsData tns; unsigned tns_present; uint8_t band_zero[SAUTI_AAC_SF_GROUPS][SAUTI_AAC_SF_BANDS]; } SautiAacLcnStage;
int sauti_aac_lcn_stage_element(const SautiAacLcnConfig *config,unsigned idx,unsigned id,const uint8_t *au,size_t length,size_t at,size_t *pos,
                                SautiAacLcnChannel ch[2],uint32_t *pns_seed,float spectral[2][1024],SautiAacLcnStage st[2],SautiAacReason *reason);
int sauti_aac_lcn_stage_cce(unsigned idx,const uint8_t *au,size_t length,size_t *pos,uint32_t *pns_seed,float spectral[1024],SautiAacLcnStage *st,SautiAacReason *reason);
int sauti_aac_lcn_stage_tns(const SautiAacLcnStage *st,unsigned idx,float spectral[1024]);
int sauti_aac_lcn_stage_synth(unsigned idx,SautiAacLcSynthesis *syn,const SautiAacLcnStage *st,const float spectral[1024],float out[1024]);
#endif
