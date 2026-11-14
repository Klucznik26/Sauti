#ifndef SAUTI_AAC_HE_PS_DELAY_H
#define SAUTI_AAC_HE_PS_DELAY_H

#include "sauti_aac_he_ps_bridge.h"

/* PS-4c1: one-frame-delay FIFO for real SBR-reconstructed QMF.
 * Push frame N with decoded PS side-data and 32 chronological slots
 * [32][64]. First push only retains frame N and produces no PCM.
 * On push N+1, the first SIX real slots of N+1 are appended to
 * the 32 stored slots from N. PS for frame N is then rendered.
 * The new frame becomes pending. Never synthesizes zero lookahead.
 *
 * End-of-stream: a pending frame is deliberately NOT flushed without
 * a genuine source of the next 6 QMF slots. Reset state after a seek.
 * This stage does not decode ADTS or reconstruct SBR HF. */
typedef struct {
    SautiAacHePsBridgeState bridge;
    SautiAacSbrPayloadFrame pending_ps;
    SautiAacSbrComplex pending_qmf[32u*64u];
    unsigned pending;
    uint64_t received_frames;
} SautiAacHePsDelayState;

/* Returns 1 for accepted frame, 0 for rejection.
 * *produced is set to 0 on warm-up, 1 after rendering.
 * On failure PCM/state are byte-identical; *produced unchanged.
 * Non-overlapping state, inputs, and output are mandatory. */
int sauti_aac_he_ps_delay_push(
    const SautiAacSbrPayloadFrame *ps,
    const SautiAacSbrComplex *qmf,size_t qmf_count,
    SautiAacHePsDelayState *state,
    float *pcm,size_t pcm_count,
    unsigned *produced);
#endif
