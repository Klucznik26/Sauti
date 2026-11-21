/* AAC-LC decoder for frameLengthFlag = 1 (960 lines per frame, 120-line short windows), see sauti_aac_lcn.h.  The bitstream tools and the filterbank are the AAC-LC ones; the 960 layout
 * (scalefactor-band tables, transform sizes, window geometry) reaches them through the layout bits of the rate index (sauti_aac_layout.h). */
#include "sauti_aac_lcn.h"
#include "sauti_aac_dequant.h"
#include "sauti_aac_headers.h"
#include "sauti_aac_pns.h"
#include "sauti_aac_pred.h"
#include "sauti_aac_adts_crc.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int get(const uint8_t *data,size_t length,size_t *pos,unsigned width,uint32_t *value){
    return sauti_aac_bits_read_asm(data,length,pos,width,value);
}
unsigned sauti_aac_lcn_frame_samples(const SautiAacLcnConfig *c){
    if(!c || c->channel_config<1u || c->channel_config>2u)return 0u;
    if(c->er){                                                              /* ER AAC LC (960 or 1024 lines) / ER AAC LTP (1024) */
        if(c->main || c->rate_index>12u)return 0u;
        if(c->ltp)return c->frame960?0u:1024u;
        return c->frame960?960u:1024u;
    }
    if(c->main && c->ltp)return 0u;
    if(c->frame960 && !c->main)return 0u;                                   /* LC 960 has its own configuration, LTP has no 960 variant */
    if(c->main)return c->rate_index<=11u?(c->frame960?960u:1024u):0u;        /* no PRED_SFB_MAX for 7350 Hz */
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
    return sauti_aac_lcn_decode_adts(cfg,state,au,length,pcm,reason,NULL,0u);
}
/* the ADTS error check of one channel element: header, then the first 192 bits after the id (zero padded) and for a CPE the first 128 bits of the second channel (zero padded) */
static int crc_ok(const uint8_t header[7],unsigned expected,const uint8_t *au,size_t length,size_t element_start,size_t element_end,size_t second_start,int cpe){
    uint16_t crc=0xffffu;
    if(element_end<element_start+3u)return 0;
    size_t first=element_end-(element_start+3u);
    if(first>192u)first=192u;
    if(!sauti_aac_adts_crc16_update_bits(header,7u,0u,56u,&crc) ||
       !sauti_aac_adts_crc16_update_bits(au,length,element_start+3u,first,&crc) ||
       !sauti_aac_adts_crc16_update_zero_bits(192u-first,&crc))return 0;
    if(cpe){
        if(second_start>element_end)return 0;
        size_t second=element_end-second_start;
        if(second>128u)second=128u;
        if(!sauti_aac_adts_crc16_update_bits(au,length,second_start,second,&crc) ||
           !sauti_aac_adts_crc16_update_zero_bits(128u-second,&crc))return 0;
    }
    return crc==(expected&0xffffu);
}
/* One channel element (SCE id 0, CPE id 1, LFE id 3) starting at bit `at` (its id included): parse, dequantise, PNS / M/S / intensity, prediction, synthesis, LTP buffer update.  The channel
 * states are advanced in place (the caller works on copies and discards them on failure); `pos` is set to the end of the element.  out[0] (and out[1] for a CPE) hold the output in decoder units. */
int sauti_aac_lcn_element(const SautiAacLcnConfig *cfg,unsigned idx,unsigned id,const uint8_t *au,size_t length,size_t at,size_t *pos,
                          SautiAacLcnChannel ch[2],uint32_t *pns_seed,float out[2][1024],SautiAacReason *reason,const uint8_t *adts_header,unsigned adts_crc){
    int status=0;
    size_t p=at;
    if(id==0u || id==3u){
        SautiAacLcMonoData *mono=(SautiAacLcMonoData *)calloc(1,sizeof(*mono));
        if(!mono)return 0;
        float spectral[1024];
        status=sauti_aac_lc_element_parse(au,length,&p,idx,id,mono,reason);
        if(status==1 && adts_header && !crc_ok(adts_header,adts_crc,au,length,at,p,0u,0)){*reason=SAUTI_AAC_REASON_INVALID_ADTS;status=0;}
        if(status==1 && (!dequantize(mono,idx,pns_seed,spectral) ||
               (cfg->main && ch[0].pred && !predict_mono(ch[0].pred,mono,idx,spectral)) ||
               (cfg->ltp && ch[0].ltp && !sauti_aac_ltp_apply(ch[0].ltp,&mono->ics,idx,ch[0].syn->previous_shape,mono->tns_present?&mono->tns:NULL,spectral)) ||
               !sauti_aac_lc_synthesis_spectral(ch[0].syn,&mono->ics,mono->tns_present?&mono->tns:NULL,idx,spectral,out[0]))){*reason=SAUTI_AAC_REASON_INVALID_SYNTH;status=0;}
        if(status==1 && cfg->ltp && ch[0].ltp)sauti_aac_ltp_update(ch[0].ltp,out[0],ch[0].syn->overlap);
        free(mono);
    }else if(id==1u){
        SautiAacCpeData *cpe=(SautiAacCpeData *)calloc(1,sizeof(*cpe));
        if(!cpe)return 0;
        float spectral[2][1024];
        status=sauti_aac_lc_cpe_parse(au,length,&p,idx,cpe,reason);
        if(status==1 && adts_header && !crc_ok(adts_header,adts_crc,au,length,at,p,cpe->crc_second_ics_bit,1)){*reason=SAUTI_AAC_REASON_INVALID_ADTS;status=0;}
        if(status==1){
            SautiAacPredState pair[2];
            const int use_pred=cfg->main && ch[0].pred && ch[1].pred;
            if(use_pred){memcpy(&pair[0],ch[0].pred,sizeof(pair[0]));memcpy(&pair[1],ch[1].pred,sizeof(pair[1]));}
            status=sauti_aac_cpe_spectra_pred(cpe,idx,pns_seed,spectral,use_pred?pair:NULL,reason);
            if(status==1 && use_pred){memcpy(ch[0].pred,&pair[0],sizeof(pair[0]));memcpy(ch[1].pred,&pair[1],sizeof(pair[1]));}
        }
        if(status==1 && cfg->ltp && ch[0].ltp && ch[1].ltp)
            for(unsigned c=0;c<2;c++)
                if(!sauti_aac_ltp_apply(ch[c].ltp,&cpe->channel[c].ics,idx,ch[c].syn->previous_shape,cpe->channel[c].tns_present?&cpe->channel[c].tns:NULL,spectral[c])){*reason=SAUTI_AAC_REASON_INVALID_SYNTH;status=0;break;}
        if(status==1)
            for(unsigned c=0;c<2;c++)
                if(!sauti_aac_lc_synthesis_spectral(ch[c].syn,&cpe->channel[c].ics,cpe->channel[c].tns_present?&cpe->channel[c].tns:NULL,idx,spectral[c],out[c])){*reason=SAUTI_AAC_REASON_INVALID_SYNTH;status=0;break;}
        if(status==1 && cfg->ltp && ch[0].ltp && ch[1].ltp)for(unsigned c=0;c<2;c++)sauti_aac_ltp_update(ch[c].ltp,out[c],ch[c].syn->overlap);
        free(cpe);
    }else{*reason=SAUTI_AAC_REASON_UNSUPPORTED_ELEMENT;return -1;}
    if(status==1)*pos=p;
    return status;
}
unsigned sauti_aac_lcn_index(const SautiAacLcnConfig *cfg){
    if(cfg->er)return SAUTI_AAC_IDX(cfg->rate_index,cfg->frame960?SAUTI_AAC_LAYOUT_960:SAUTI_AAC_LAYOUT_1024)|SAUTI_AAC_IDX_ER_FLAG|(cfg->ltp?SAUTI_AAC_IDX_LTP_FLAG:0u);
    return cfg->main?(SAUTI_AAC_IDX(cfg->rate_index,cfg->frame960?SAUTI_AAC_LAYOUT_960:SAUTI_AAC_LAYOUT_1024)|SAUTI_AAC_IDX_MAIN_FLAG):cfg->ltp?(SAUTI_AAC_IDX(cfg->rate_index,SAUTI_AAC_LAYOUT_1024)|SAUTI_AAC_IDX_LTP_FLAG):SAUTI_AAC_IDX(cfg->rate_index,SAUTI_AAC_LAYOUT_960);
}
int sauti_aac_lcn_decode_adts(const SautiAacLcnConfig *cfg,SautiAacLcnState *state,const uint8_t *au,size_t length,float *pcm,SautiAacReason *reason,const uint8_t adts_header[7],unsigned adts_crc){
    SautiAacReason local=SAUTI_AAC_REASON_INVALID_SCE;
    if(!reason)reason=&local;
    *reason=SAUTI_AAC_REASON_INVALID_SCE;
    const unsigned n=sauti_aac_lcn_frame_samples(cfg);
    if(!n || !state || !au || !pcm || length>SIZE_MAX/8u)return 0;
    const unsigned idx=sauti_aac_lcn_index(cfg);
    const unsigned channels=cfg->channel_config;
    SautiAacLcnState next=*state;
    float out[2][1024],mix[2*1024];
    int status=0,done=0;
    size_t pos=0;
    for(;;){                                                                  /* raw_data_block(): elements until ID_END (ER syntax: exactly the elements of the channel configuration, no ids) */
        size_t at=pos;
        uint32_t id=0;
        if(cfg->er){
            if(done)break;
            id=channels==1u?0u:1u;
        }else{
        if(!get(au,length,&pos,3,&id))return 0;
        if(id==7u)break;
        if(id==6u){                                                           /* fill element: SBR / PS would change the output */
            const int s=sauti_aac_fill_skip_non_sbr(au,length,&pos,NULL);
            if(s!=1){*reason=s==-1?SAUTI_AAC_REASON_UNSUPPORTED_FIL:SAUTI_AAC_REASON_INVALID_SCE;return s==-1?-1:0;}
            continue;
        }
        if(id==4u){if(!skip_dse(au,length,&pos))return 0;continue;}
        }
        if(done || (channels==1u && id!=0u) || (channels==2u && id!=1u)){*reason=SAUTI_AAC_REASON_UNSUPPORTED_ELEMENT;return -1;}
        SautiAacLcnChannel ch[2]={{&next.channel[0],&next.pred[0],&next.ltp[0]},{&next.channel[1],&next.pred[1],&next.ltp[1]}};
        uint32_t seed=next.channel[0].pns_seed;
        status=sauti_aac_lcn_element(cfg,idx,id,au,length,at,&pos,ch,&seed,out,reason,adts_header,adts_crc);
        if(status!=1)return status;
        next.channel[0].pns_seed=seed;if(id==1u)next.channel[1].pns_seed=seed;
        done=1;
    }
    if(!done)return 0;
    for(unsigned i=0;i<n;i++){
        for(unsigned c=0;c<channels;c++){
            const float v=ldexpf(out[c][i],-16);
            if(!isfinite(v)){*reason=SAUTI_AAC_REASON_INVALID_SYNTH;return 0;}
            mix[i*channels+c]=v;
        }
    }
    memcpy(pcm,mix,(size_t)n*channels*sizeof(float));
    *state=next;
    *reason=SAUTI_AAC_REASON_OK;
    return 1;
}
