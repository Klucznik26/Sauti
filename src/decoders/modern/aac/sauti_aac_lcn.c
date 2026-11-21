/* AAC-LC decoder for frameLengthFlag = 1 (960 lines per frame, 120-line short windows), see sauti_aac_lcn.h.  The bitstream tools and the filterbank are the AAC-LC ones; the 960 layout
 * (scalefactor-band tables, transform sizes, window geometry) reaches them through the layout bits of the rate index (sauti_aac_layout.h). */
#include "sauti_aac_lcn.h"
#include "sauti_aac_dequant.h"
#include "sauti_aac_headers.h"
#include "sauti_aac_pns.h"
#include "sauti_aac_pred.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int get(const uint8_t *data,size_t length,size_t *pos,unsigned width,uint32_t *value){
    return sauti_aac_bits_read_asm(data,length,pos,width,value);
}
unsigned sauti_aac_lcn_frame_samples(const SautiAacLcnConfig *c){
    if(!c || c->channel_config<1u || c->channel_config>2u)return 0u;
    if(c->main && c->ltp)return 0u;
    if(c->main)return c->rate_index<=11u?1024u:0u;                          /* no PRED_SFB_MAX for 7350 Hz */
    if(c->ltp)return c->rate_index<=12u?1024u:0u;
    return c->rate_index<=12u?960u:0u;
}
void sauti_aac_lcn_state_init(SautiAacLcnState *state){
    memset(state,0,sizeof(*state));
    for(unsigned c=0;c<2;c++){(void)sauti_aac_lc_synthesis_init(&state->channel[c],0u);sauti_aac_pred_reset(&state->pred[c]);sauti_aac_ltp_reset(&state->ltp[c]);}
}
/* data_stream_element(): tag(4), data_byte_align_flag(1), count(8) [+ esc_count(8)], optional byte alignment, count bytes */
static int skip_dse(const uint8_t *data,size_t length,size_t *pos){
    uint32_t tag=0,align=0,count=0,esc=0;
    if(!get(data,length,pos,4,&tag) || !get(data,length,pos,1,&align) || !get(data,length,pos,8,&count))return 0;
    if(count==255u){if(!get(data,length,pos,8,&esc))return 0;count+=esc;}
    if(align)*pos=(*pos+7u)&~(size_t)7u;
    if(*pos>length*8u || (size_t)count*8u>length*8u-*pos)return 0;
    *pos+=(size_t)count*8u;
    return 1;
}
static int predict_mono(SautiAacPredState *pred,const SautiAacLcMonoData *d,unsigned idx,float spectral[1024]){
    uint8_t noise[SAUTI_AAC_SF_BANDS];
    for(unsigned b=0;b<SAUTI_AAC_SF_BANDS;b++)noise[b]=d->factors.codebook[0][b]==13u;
    return sauti_aac_pred_apply_pns(pred,&d->ics,idx,spectral,noise);
}
static int dequantize(const SautiAacLcMonoData *d,unsigned idx,uint32_t *seed,float out[1024]){
    return sauti_aac_lc_dequantize(d->quant,idx,&d->ics,&d->sections,&d->factors,out) &&
           sauti_aac_pns_apply(&d->ics,idx,&d->sections,&d->factors,seed,out);
}
int sauti_aac_lcn_decode(const SautiAacLcnConfig *cfg,SautiAacLcnState *state,const uint8_t *au,size_t length,float *pcm,SautiAacReason *reason){
    SautiAacReason local=SAUTI_AAC_REASON_INVALID_SCE;
    if(!reason)reason=&local;
    *reason=SAUTI_AAC_REASON_INVALID_SCE;
    const unsigned n=sauti_aac_lcn_frame_samples(cfg);
    if(!n || !state || !au || !pcm || length>SIZE_MAX/8u)return 0;
    const unsigned idx=cfg->main?(SAUTI_AAC_IDX(cfg->rate_index,SAUTI_AAC_LAYOUT_1024)|SAUTI_AAC_IDX_MAIN_FLAG):cfg->ltp?(SAUTI_AAC_IDX(cfg->rate_index,SAUTI_AAC_LAYOUT_1024)|SAUTI_AAC_IDX_LTP_FLAG):SAUTI_AAC_IDX(cfg->rate_index,SAUTI_AAC_LAYOUT_960);
    const unsigned channels=cfg->channel_config;
    SautiAacLcnState next=*state;
    float out[2][1024],mix[2*1024];
    int status=0,done=0;
    size_t pos=0;
    SautiAacLcMonoData *mono=NULL;SautiAacCpeData *cpe=NULL;
    for(;;){                                                                  /* raw_data_block(): elements until ID_END */
        size_t at=pos;
        uint32_t id=0;
        if(!get(au,length,&pos,3,&id)){status=0;goto done;}
        if(id==7u)break;
        if(id==6u){                                                           /* fill element: SBR / PS would change the output */
            const int s=sauti_aac_fill_skip_non_sbr(au,length,&pos,NULL);
            if(s!=1){*reason=s==-1?SAUTI_AAC_REASON_UNSUPPORTED_FIL:SAUTI_AAC_REASON_INVALID_SCE;status=s==-1?-1:0;goto done;}
            continue;
        }
        if(id==4u){if(!skip_dse(au,length,&pos)){status=0;goto done;}continue;}
        if(done || (channels==1u && id!=0u) || (channels==2u && id!=1u)){*reason=SAUTI_AAC_REASON_UNSUPPORTED_ELEMENT;status=-1;goto done;}
        if(channels==1u){
            mono=(SautiAacLcMonoData *)calloc(1,sizeof(*mono));
            if(!mono){status=0;goto done;}
            pos=at;
            status=sauti_aac_lc_element_parse(au,length,&pos,idx,0u,mono,reason);
            if(status!=1)goto done;
            float spectral[1024];
            if(!dequantize(mono,idx,&next.channel[0].pns_seed,spectral) ||
               (cfg->main && !predict_mono(&next.pred[0],mono,idx,spectral)) ||
               (cfg->ltp && !sauti_aac_ltp_apply(&next.ltp[0],&mono->ics,idx,next.channel[0].previous_shape,mono->tns_present?&mono->tns:NULL,spectral)) ||
               !sauti_aac_lc_synthesis_spectral(&next.channel[0],&mono->ics,mono->tns_present?&mono->tns:NULL,idx,spectral,out[0])){*reason=SAUTI_AAC_REASON_INVALID_SYNTH;status=0;goto done;}
        }else{
            cpe=(SautiAacCpeData *)calloc(1,sizeof(*cpe));
            if(!cpe){status=0;goto done;}
            pos=at;
            status=sauti_aac_lc_cpe_parse(au,length,&pos,idx,cpe,reason);
            if(status!=1)goto done;
            float spectral[2][1024];
            uint32_t seed=next.channel[0].pns_seed;
            status=sauti_aac_cpe_spectra_pred(cpe,idx,&seed,spectral,cfg->main?next.pred:NULL,reason);
            if(status!=1)goto done;
            if(cfg->ltp)
                for(unsigned c=0;c<2;c++)
                    if(!sauti_aac_ltp_apply(&next.ltp[c],&cpe->channel[c].ics,idx,next.channel[c].previous_shape,cpe->channel[c].tns_present?&cpe->channel[c].tns:NULL,spectral[c])){*reason=SAUTI_AAC_REASON_INVALID_SYNTH;status=0;goto done;}
            next.channel[0].pns_seed=next.channel[1].pns_seed=seed;
            for(unsigned c=0;c<2;c++)
                if(!sauti_aac_lc_synthesis_spectral(&next.channel[c],&cpe->channel[c].ics,cpe->channel[c].tns_present?&cpe->channel[c].tns:NULL,idx,spectral[c],out[c])){
                    *reason=SAUTI_AAC_REASON_INVALID_SYNTH;status=0;goto done;
                }
        }
        if(cfg->ltp)for(unsigned c=0;c<channels;c++)sauti_aac_ltp_update(&next.ltp[c],out[c],next.channel[c].overlap);
        done=1;
    }
    status=done?1:0;
    if(status==1){
        for(unsigned i=0;i<n;i++){
            for(unsigned c=0;c<channels;c++){
                const float v=ldexpf(out[c][i],-16);
                if(!isfinite(v)){*reason=SAUTI_AAC_REASON_INVALID_SYNTH;status=0;goto done;}
                mix[i*channels+c]=v;
            }
        }
        memcpy(pcm,mix,(size_t)n*channels*sizeof(float));
        *state=next;
        *reason=SAUTI_AAC_REASON_OK;
    }
done:
    free(mono);free(cpe);
    return status;
}
