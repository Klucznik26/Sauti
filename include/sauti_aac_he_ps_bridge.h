#ifndef SAUTI_AAC_HE_PS_BRIDGE_H
#define SAUTI_AAC_HE_PS_BRIDGE_H

#include "sauti_aac_sbr_payload.h"
#include "sauti_aac_ps_render.h"
#include "sauti_aac_ps_pcm_stereo.h"

/* AAC-M1 PS-4a: bridge a REAL SBR parser's retained PS parameters
 * into the existing 32-slot stereo synthesis pipeline.
 *
 * Precondition: mono_qmf is already reconstructed and time-aligned by
 * a verified SBR HF driver; [38][64] complex QMF slots, including
 * six true forward-lookahead slots. No upsampling or fake HF here.
 *
 * This API does NOT read ADTS, synthesize HF, or resolve SBR temporal
 * handoffs; therefore it is NOT a full HE-AAC v2 decoder. */
typedef struct {
    SautiAacPsRenderState render;
    SautiAacPsPcmPipelineState pcm;
    uint64_t frames;
} SautiAacHePsBridgeState;

/* 1 = complete frame, 0 = invalid/unsupported. All state and PCM
 * commit atomically. A payload without valid PS is rejected.
 * Reset the whole state to zero after seek or stream discontinuity. */
int sauti_aac_he_ps_bridge_render(
    const SautiAacSbrPayloadFrame *payload,
    const SautiAacSbrComplex *mono_qmf,size_t qmf_count,
    SautiAacHePsBridgeState *state,
    float *stereo_pcm,size_t stereo_pcm_count);

#endif
