#ifndef SAUTI_AAC_HE_ADTS_PS_H
#define SAUTI_AAC_HE_ADTS_PS_H

#include "sauti_aac_he_adts_mono.h"
#include "sauti_aac_he_ps_delay.h"
#include "sauti_aac_sbr_qmf_frame.h"

/* PS-4c2: experimental native ADTS (mono AAC-LC SCE + SBR + PS)
 * to stereo PCM. Uses the actual SBR-29/30 parser, reconstructed
 * HF QMF from SBR-28/PS-4b, and the six-slot PS-4c1 FIFO.
 * Strict SBR grid subset only. No fake high-band data or lookahead.
 * First ADTS frame publishes NO PCM; from the second valid frame
 * each call publishes 4096 interleaved L/R float samples for the
 * PREVIOUS frame. Do not claim ISO conformance before reference tests.
 *
 * Reset after seek. At EOS the remaining pending frame is not emitted
 * until a verified drain procedure is implemented. */
typedef struct {
    SautiAacHeAdtsMonoState adts;
    SautiAacSbrQmfFrameState qmf;
    SautiAacHePsDelayState ps;
    uint64_t frames;
} SautiAacHeAdtsPsState;

int sauti_aac_he_adts_ps_init(SautiAacHeAdtsPsState *state);

/* 1 accepted, 0 invalid, -1 unsupported. On success:
 * produced=0 warm-up or 1 with 4096 previous-frame samples.
 * report refers to the INPUT frame's SBR HF adjustment.
 * Output, report, produced and entire state commit atomically;
 * reason reports a rejection where possible. */
int sauti_aac_he_adts_ps_decode_ex(
    const uint8_t *adts,size_t adts_bytes,
    SautiAacHeAdtsPsState *state,
    float *stereo_pcm,size_t pcm_count,
    unsigned *produced,
    SautiAacSbrLimiterReport *report,
    SautiAacReason *reason);

#endif
