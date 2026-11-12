#include "sauti_aac_he_adts_ps.h"
#include <math.h>
#include <stdint.h>
#include <string.h>

int sauti_aac_he_adts_ps_init(SautiAacHeAdtsPsState *state){
    if(!state)return 0;
    SautiAacHeAdtsPsState next={0};
    if(!sauti_aac_he_adts_mono_init(&next.adts))return 0;
    *state=next;
    return 1;
}
int sauti_aac_he_adts_ps_decode_ex(
    const uint8_t *adts,size_t adts_bytes,
    SautiAacHeAdtsPsState *state,
    float *stereo_pcm,size_t pcm_count,
    unsigned *produced,
    SautiAacSbrLimiterReport *report,
    SautiAacReason *reason){
    if(!reason)return 0;
    *reason=SAUTI_AAC_REASON_INVALID_ADTS;
    if(!adts||!state||!stereo_pcm||!produced||!report||
       pcm_count<4096u||state->frames==UINT64_MAX||
       state->frames!=state->adts.frames||
       state->frames!=state->qmf.frames||
       state->frames!=state->ps.received_frames||
       (state->frames==0u&&state->qmf.ready)||
       (state->frames!=0u&&!state->qmf.ready)){
        *reason=SAUTI_AAC_REASON_UNSUPPORTED_HISTORY;
        return 0;
    }
    SautiAacHeAdtsPsState next=*state;
    SautiAacSbrPayloadFrame payload;
    float core_pcm[1024u],rendered[4096u];
    SautiAacSbrComplex reconstructed[32u*64u];
    SautiAacSbrLimiterReport stats;
    const int status=sauti_aac_he_adts_mono_decode_ex(
        adts,adts_bytes,&next.adts,core_pcm,&payload,reason);
    if(status!=1)return status;
    if(payload.has_ps!=1u){
        *reason=SAUTI_AAC_REASON_UNSUPPORTED_FIL;
        return -1;
    }
    SautiAacSbrHfJointConfig hf;
    if(!sauti_aac_sbr_payload_make_hf(&payload,&hf)||
       hf.slots!=32u){
        *reason=SAUTI_AAC_REASON_INVALID_SYNTH;
        return -1;
    }
    const SautiAacSbrQmfFrameConfig config={
        .hf=&hf,.attack=payload.attack,
        .bs_smoothing_mode=payload.bs_smoothing_mode,
        .lpc=NULL,.chirp=NULL
    };
    if(!sauti_aac_sbr_qmf_frame_export_qmf_ex(
           &config,core_pcm,1024u,
           reconstructed,32u*64u,&next.qmf,&stats,NULL)){
        *reason=SAUTI_AAC_REASON_INVALID_SYNTH;
        return -1;
    }
    unsigned ready=0u;
    if(!sauti_aac_he_ps_delay_push(
           &payload,reconstructed,32u*64u,
           &next.ps,rendered,4096u,&ready)){
        *reason=SAUTI_AAC_REASON_INVALID_SYNTH;
        return -1;
    }
    next.frames++;
    if(ready){
        for(unsigned i=0;i<4096u;i++)
            if(!isfinite(rendered[i])){
                *reason=SAUTI_AAC_REASON_INVALID_SYNTH;
                return 0;
            }
        memcpy(stereo_pcm,rendered,4096u*sizeof(*stereo_pcm));
    }
    *report=stats;
    *produced=ready;
    *state=next;
    *reason=SAUTI_AAC_REASON_OK;
    return 1;
}
