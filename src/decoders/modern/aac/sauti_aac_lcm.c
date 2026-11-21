/* AAC Main / LTP with more than two channels, see sauti_aac_lcm.h. */
#include "sauti_aac_lcm.h"
#include "sauti_aac_headers.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int get(const uint8_t *data,size_t length,size_t *pos,unsigned width,uint32_t *value){
    return sauti_aac_bits_read_asm(data,length,pos,width,value);
}
unsigned sauti_aac_lcm_channels(const SautiAacLcnConfig *c){
    if(!c || c->channel_config<3u || c->channel_config>7u || (!c->main && !c->ltp && !c->er) || (c->main && (c->ltp || c->er)) || c->frame960)return 0u;
    if(c->main?c->rate_index>11u:c->rate_index>12u)return 0u;
    return sauti_aac_lc_multi_config_channels(c->channel_config);
}
void sauti_aac_lcm_state_init(SautiAacLcmState *state,const SautiAacLcnConfig *c){
    memset(state,0,sizeof(*state));
    state->channels=sauti_aac_lcm_channels(c);state->config=c->channel_config;
    for(unsigned i=0;i<state->channels;i++){
        (void)sauti_aac_lc_synthesis_init(&state->syn[i],0u);
        if(c->main)sauti_aac_pred_reset(&state->u.pred[i]);else if(c->ltp)sauti_aac_ltp_reset(&state->u.ltp[i]);
    }
    state->pns_seed=state->syn[0].pns_seed;
}
/* data_stream_element: tag(4) align(1) count(8)[+esc(8)] [align] bytes */
static int skip_dse(const uint8_t *raw,size_t length,size_t *pos){
    uint32_t tag=0,align=0,count=0,esc=0;
    if(!get(raw,length,pos,4,&tag)||!get(raw,length,pos,1,&align)||!get(raw,length,pos,8,&count))return 0;
    if(count==255u){if(!get(raw,length,pos,8,&esc))return 0;count+=esc;}
    if(align)*pos=(*pos+7u)&~(size_t)7u;
    if(*pos>length*8u||(size_t)count*8u>length*8u-*pos)return 0;
    *pos+=(size_t)count*8u;
    return 1;
}
int sauti_aac_lcm_decode(const SautiAacLcnConfig *cfg,SautiAacLcmState *state,const uint8_t *au,size_t length,float *pcm,SautiAacReason *reason){
    SautiAacReason local=SAUTI_AAC_REASON_INVALID_SCE;
    if(!reason)reason=&local;
    *reason=SAUTI_AAC_REASON_INVALID_SCE;
    const unsigned channels=sauti_aac_lcm_channels(cfg);
    if(!channels||!state||!au||!pcm||length>SIZE_MAX/8u||state->channels!=channels||state->config!=cfg->channel_config)return 0;
    const unsigned idx=sauti_aac_lcn_index(cfg);
    SautiAacLcmState *next=(SautiAacLcmState *)malloc(sizeof(*next));         /* atomic: work on a copy */
    float (*stage)[1024]=(float (*)[1024])malloc(sizeof(float)*SAUTI_AAC_LC_MULTI_MAX*1024u);
    float *outbuf=(float *)malloc(sizeof(float)*SAUTI_AAC_LC_MULTI_MAX*1024u);
    int status=0;
    if(!next||!stage||!outbuf){free(next);free(stage);free(outbuf);return 0;}
    memcpy(next,state,sizeof(*next));
    size_t pos=0;unsigned used=0;uint32_t seed=next->pns_seed;
    static const char *const layout[8]={"","S","C","SC","SCS","SCC","SCCL","SCCCL"};       /* ER syntax: the elements of the channel configuration, in this order */
    const char *erel=layout[cfg->channel_config];
    for(;;){
        size_t at=pos;uint32_t id=0;
        if(cfg->er){
            if(!*erel)break;
            id=*erel=='S'?0u:*erel=='C'?1u:3u;erel++;
        }else{
        if(!get(au,length,&pos,3,&id)){*reason=SAUTI_AAC_REASON_INVALID_TRAILING;goto fail0;}
        if(id==7u)break;
        if(id==4u){if(!skip_dse(au,length,&pos)){*reason=SAUTI_AAC_REASON_INVALID_TRAILING;goto fail0;}continue;}
        if(id==6u){
            const int sk=sauti_aac_fill_skip_non_sbr(au,length,&pos,NULL);
            if(sk==-1){*reason=SAUTI_AAC_REASON_UNSUPPORTED_FIL;status=-1;goto fail;}
            if(sk!=1){*reason=SAUTI_AAC_REASON_INVALID_TRAILING;goto fail0;}
            continue;
        }
        }
        if(id==0u||id==3u||id==1u){
            const unsigned need=id==1u?2u:1u;
            if(used+need>channels){*reason=SAUTI_AAC_REASON_UNSUPPORTED_ELEMENT;goto fail0;}
            SautiAacLcnChannel ch[2];float out[2][1024];
            for(unsigned k=0;k<need;k++){
                ch[k].syn=&next->syn[used+k];
                ch[k].pred=cfg->main?&next->u.pred[used+k]:NULL;
                ch[k].ltp=cfg->ltp?&next->u.ltp[used+k]:NULL;
            }
            if(need==1u){ch[1]=ch[0];}                                       /* unused */
            status=sauti_aac_lcn_element(cfg,idx,id,au,length,at,&pos,ch,&seed,out,reason,NULL,0u);
            if(status!=1)goto fail;
            for(unsigned k=0;k<need;k++)memcpy(stage[used+k],out[k],sizeof(float)*1024u);
            used+=need;
            continue;
        }
        *reason=SAUTI_AAC_REASON_UNSUPPORTED_ELEMENT;status=-1;goto fail;       /* PCE (5), CCE (2) */
    }
    while(pos&7u){
        uint32_t pad=1;
        if(!get(au,length,&pos,1,&pad)||pad){*reason=SAUTI_AAC_REASON_INVALID_TRAILING;goto fail0;}
    }
    if((!cfg->er && pos!=length*8u)||pos>length*8u||used!=channels){*reason=SAUTI_AAC_REASON_INVALID_TRAILING;goto fail0;}
    next->pns_seed=seed;
    {
        uint8_t map[SAUTI_AAC_LC_MULTI_MAX];
        if(!sauti_aac_lc_multi_output_map(cfg->channel_config,map)){*reason=SAUTI_AAC_REASON_UNSUPPORTED_ADTS;status=-1;goto fail;}
        for(unsigned c=0;c<channels;c++)
            for(unsigned i=0;i<1024u;i++){
                const float v=ldexpf(stage[map[c]][i],-16);
                if(!isfinite(v)){*reason=SAUTI_AAC_REASON_INVALID_SYNTH;goto fail0;}
                outbuf[(size_t)i*channels+c]=v;
            }
    }
    memcpy(state,next,sizeof(*state));
    memcpy(pcm,outbuf,sizeof(float)*1024u*channels);
    *reason=SAUTI_AAC_REASON_OK;
    free(next);free(stage);free(outbuf);
    return 1;
fail0:
    status=0;
fail:
    free(next);free(stage);free(outbuf);
    return status;
}
