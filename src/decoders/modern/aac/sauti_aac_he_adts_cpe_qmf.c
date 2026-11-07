#include "sauti_aac_he_adts_cpe_qmf.h"
#include <math.h>
#include <string.h>

int sauti_aac_he_adts_cpe_qmf_init(SautiAacHeAdtsCpeQmfState *state){
    if(!state)return 0;
    SautiAacHeAdtsCpeQmfState next={0};
    if(!sauti_aac_he_adts_cpe_init(&next.adts))return 0;
    *state=next;
    return 1;
}
int sauti_aac_he_adts_cpe_qmf_decode_ex(
    const uint8_t *frame,size_t frame_bytes,
    SautiAacHeAdtsCpeQmfState *state,float output[4096],
    SautiAacSbrLimiterReport reports[2],SautiAacReason *reason){
    if(!reason)return 0;
    *reason=SAUTI_AAC_REASON_INVALID_ADTS;
    if(!frame || !state || !output || !reports || state->frames==UINT64_MAX ||
       state->frames!=state->adts.frames ||
       state->frames!=state->qmf[0].frames ||
       state->frames!=state->qmf[1].frames ||
       (state->frames==0u && (state->qmf[0].ready||state->qmf[1].ready)) ||
       (state->frames!=0u && (!state->qmf[0].ready||!state->qmf[1].ready))){
        *reason=SAUTI_AAC_REASON_UNSUPPORTED_HISTORY;
        return 0;
    }
    SautiAacHeAdtsCpeQmfState next=*state;
    SautiAacSbrPayloadCpeFrame payload;
    float core[2048],rendered[2][2048];
    const int decoded=sauti_aac_he_adts_cpe_decode_ex(
        frame,frame_bytes,&next.adts,core,&payload,reason);
    if(decoded!=1)return decoded;
    for(unsigned ch=0u;ch<2u;ch++){
        SautiAacSbrHfJointConfig hf;
        if(!sauti_aac_sbr_payload_cpe_make_hf(&payload,ch,&hf)){
            *reason=SAUTI_AAC_REASON_INVALID_SYNTH;return -1;
        }
        float channel_core[1024];
        for(unsigned i=0u;i<1024u;i++)channel_core[i]=core[2u*i+ch];
        const SautiAacSbrQmfFrameConfig config={
            .hf=&hf,.attack=payload.channel[ch].attack,
            .bs_smoothing_mode=payload.channel[ch].bs_smoothing_mode,
            .lpc=NULL,.chirp=NULL
        };
        if(!sauti_aac_sbr_qmf_frame_process(
               &config,channel_core,1024u,rendered[ch],2048u,
               &next.qmf[ch],&reports[ch])){
            *reason=SAUTI_AAC_REASON_INVALID_SYNTH;return -1;
        }
        for(unsigned i=0u;i<2048u;i++)
            if(!isfinite(rendered[ch][i])){
                *reason=SAUTI_AAC_REASON_INVALID_SYNTH;return 0;
            }
    }
    for(unsigned i=0u;i<2048u;i++){
        output[2u*i]=rendered[0][i];
        output[2u*i+1u]=rendered[1][i];
    }
    next.frames++;*state=next;*reason=SAUTI_AAC_REASON_OK;
    return 1;
}
