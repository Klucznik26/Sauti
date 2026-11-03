/* HE-AAC SBR-31: real ADTS AAC-LC+SBR -> 2048-sample experimental
 * QMF waveform, combining SBR-30 and SBR-28 atomically.
 *
 * No external codec dependencies, no external PCM source. An SBR
 * grammar/conformance gap is an unsupported frame, not "silence" or
 * fabricated high-band audio. Normal AAC-LC decoder remains unchanged.
 */
#include "sauti_aac_he_adts_qmf.h"
#include <math.h>
#include <string.h>

int sauti_aac_he_adts_qmf_init(SautiAacHeAdtsQmfState *state){
    if(!state)return 0;
    SautiAacHeAdtsQmfState next={0};
    if(!sauti_aac_he_adts_mono_init(&next.adts))return 0;
    *state=next;
    return 1;
}

int sauti_aac_he_adts_qmf_decode_ex(
    const uint8_t *frame,size_t frame_bytes,
    SautiAacHeAdtsQmfState *state,
    float pcm_2x[2048],
    SautiAacSbrLimiterReport *report,
    SautiAacReason *reason){
    if(!reason)return 0;
    *reason=SAUTI_AAC_REASON_INVALID_ADTS;
    if(!frame || !state || !pcm_2x || !report ||
       state->frames==UINT64_MAX ||
       state->frames!=state->adts.frames ||
       state->frames!=state->qmf.frames ||
       (state->frames==0u && state->qmf.ready) ||
       (state->frames!=0u && !state->qmf.ready)){
        *reason=SAUTI_AAC_REASON_UNSUPPORTED_HISTORY;
        return 0;
    }

    SautiAacHeAdtsQmfState next=*state;
    SautiAacSbrPayloadFrame payload;
    float core[1024],rendered[2048];
    SautiAacSbrLimiterReport stats;
    const int status=sauti_aac_he_adts_mono_decode_ex(
        frame,frame_bytes,&next.adts,core,&payload,reason);
    if(status!=1)return status;

    SautiAacSbrHfJointConfig hf;
    if(!sauti_aac_sbr_payload_make_hf(&payload,&hf)){
        *reason=SAUTI_AAC_REASON_INVALID_SYNTH;
        return -1;
    }
    const SautiAacSbrQmfFrameConfig config={
        .hf=&hf,.attack=payload.attack,
        .bs_smoothing_mode=payload.bs_smoothing_mode,
        .lpc=NULL,.chirp=NULL
    };
    if(!sauti_aac_sbr_qmf_frame_process(
           &config,core,1024u,rendered,2048u,
           &next.qmf,&stats)){
        /* This initial pipeline intentionally declines to invent QMF
         * source energy when a decoded SBR envelope targets unpatched
         * bands; better reject an unsupported valid frame than fake it.
         * Other not-yet-matched timing/envelope cases reject likewise. */
        *reason=SAUTI_AAC_REASON_INVALID_SYNTH;
        return -1;
    }
    for(unsigned i=0;i<2048u;i++)
        if(!isfinite(rendered[i])){
            *reason=SAUTI_AAC_REASON_INVALID_SYNTH;
            return 0;
        }

    next.frames++;
    memcpy(pcm_2x,rendered,sizeof(rendered));
    *report=stats;
    *state=next;
    *reason=SAUTI_AAC_REASON_OK;
    return 1;
}
