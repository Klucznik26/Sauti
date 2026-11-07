#ifndef SAUTI_AAC_HE_ADTS_CPE_QMF_H
#define SAUTI_AAC_HE_ADTS_CPE_QMF_H

#include "sauti_aac_he_adts_cpe.h"
#include "sauti_aac_sbr_qmf_frame.h"

/* Experimental whole-frame HE-AAC stereo CPE path. Supported input is one
 * ADTS AAC-LC CPE with one SBR CPE type 13 or verified type 14
 * FIL. Output is 2048 stereo frames (4096 interleaved f32 samples) at twice
 * the core sample rate. No fallback is produced for PS, ADTS CRC or other
 * unsupported syntax. */
typedef struct {
    SautiAacHeAdtsCpeState adts;
    SautiAacSbrQmfFrameState qmf[2];
    uint64_t frames;
} SautiAacHeAdtsCpeQmfState;

int sauti_aac_he_adts_cpe_qmf_init(SautiAacHeAdtsCpeQmfState *state);
int sauti_aac_he_adts_cpe_qmf_decode_ex(
    const uint8_t *frame,size_t frame_bytes,
    SautiAacHeAdtsCpeQmfState *state,float pcm_2x_interleaved[4096],
    SautiAacSbrLimiterReport reports[2],SautiAacReason *reason);

#endif
