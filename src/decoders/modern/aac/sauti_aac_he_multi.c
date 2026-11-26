/* HE-AAC with any channel layout: sauti_aac_lcw decodes the core of every element, the SBR extension of each SCE / CPE goes through its own payload state and SBR channels (see the header). */
#include "sauti_aac_he_multi.h"
#include "sauti_aac_lc_multi.h"
#include "sauti_aac_sbr_channel.h"
#include "sauti_aac_sbr_payload.h"
#include <stdlib.h>
#include <string.h>

typedef struct { uint8_t type,tag,slot; } Elem;      /* type 0 SCE, 1 CPE, 3 LFE */
struct SautiAacHeMulti {
    SautiAacLcw *lcw;
    SautiAacLcwConfig cfg;
    unsigned channels,nelem,core_rate,downsampled;
    Elem elem[SAUTI_AAC_LCW_MAX_CHANNELS];
    SautiAacSbrPayloadState *mono;                   /* [nelem] (used for SCE) */
    SautiAacSbrPayloadCpeState *cpe;                 /* [nelem] (used for CPE) */
    SautiAacSbrChannel *channel;                     /* [channels], by slot */
};
static unsigned elements_of(unsigned config,Elem *e){
    static const char *const pattern[8]={"","","","SC","SCS","SCC","SCCL","SCCCL"};
    if(config<3u||config>7u)return 0;
    unsigned n=0,slot=0,sce=0,cpe=0;
    for(const char *p=pattern[config];*p;p++){
        e[n].slot=(uint8_t)slot;
        if(*p=='S'){e[n].type=0;e[n].tag=(uint8_t)sce++;slot++;}
        else if(*p=='C'){e[n].type=1;e[n].tag=(uint8_t)cpe++;slot+=2u;}
        else{e[n].type=3;e[n].tag=0;slot++;}
        n++;
    }
    return n;
}
static int alloc_arrays(SautiAacHeMulti *s){
    s->mono=(SautiAacSbrPayloadState *)calloc(s->nelem,sizeof(*s->mono));
    s->cpe=(SautiAacSbrPayloadCpeState *)calloc(s->nelem,sizeof(*s->cpe));
    s->channel=(SautiAacSbrChannel *)calloc(s->channels,sizeof(*s->channel));
    return s->mono&&s->cpe&&s->channel;
}
void sauti_aac_he_multi_free(SautiAacHeMulti *s){
    if(!s)return;
    sauti_aac_lcw_free(s->lcw);
    free(s->mono);free(s->cpe);free(s->channel);free(s);
}
SautiAacHeMulti *sauti_aac_he_multi_new(const SautiAacLcwConfig *cfg,unsigned config,unsigned core_rate,unsigned downsampled){
    if(!cfg||!core_rate||core_rate>96000u)return NULL;
    SautiAacHeMulti *s=(SautiAacHeMulti *)calloc(1,sizeof(*s));
    if(!s)return NULL;
    s->cfg=*cfg;s->core_rate=core_rate;s->downsampled=downsampled?1u:0u;
    if(config){
        if(!sauti_aac_lc_multi_config_channels(config)){free(s);return NULL;}
        s->nelem=elements_of(config,s->elem);
        memset(&s->cfg.layout,0,sizeof(s->cfg.layout));
        s->cfg.layout.channels=sauti_aac_lc_multi_config_channels(config);
        if(!s->nelem||!sauti_aac_lc_multi_output_map(config,s->cfg.layout.map)){free(s);return NULL;}
    }else{
        const SautiAacLcwLayout *l=&cfg->layout;
        if(!l->n_elements||l->channels<1u){free(s);return NULL;}                       /* repeated tags: the elements cannot be told apart */
        s->nelem=l->n_elements;
        for(unsigned e=0;e<s->nelem;e++){s->elem[e].type=l->elem_type[e];s->elem[e].tag=l->elem_tag[e];s->elem[e].slot=l->elem_slot[e];}
    }
    s->channels=s->cfg.layout.channels;
    s->lcw=sauti_aac_lcw_new(&s->cfg);
    if(!s->lcw||!alloc_arrays(s)){sauti_aac_he_multi_free(s);return NULL;}
    for(unsigned c=0;c<s->channels;c++){
        if(!sauti_aac_sbr_channel_init(&s->channel[c])){sauti_aac_he_multi_free(s);return NULL;}
        s->channel[c].downsampled=s->downsampled;
    }
    return s;
}
SautiAacHeMulti *sauti_aac_he_multi_dup(const SautiAacHeMulti *o){
    if(!o)return NULL;
    SautiAacHeMulti *s=(SautiAacHeMulti *)malloc(sizeof(*s));
    if(!s)return NULL;
    *s=*o;s->lcw=NULL;s->mono=NULL;s->cpe=NULL;s->channel=NULL;
    if(!alloc_arrays(s)||!(s->lcw=sauti_aac_lcw_dup(o->lcw))){sauti_aac_he_multi_free(s);return NULL;}
    memcpy(s->mono,o->mono,s->nelem*sizeof(*s->mono));
    memcpy(s->cpe,o->cpe,s->nelem*sizeof(*s->cpe));
    memcpy(s->channel,o->channel,s->channels*sizeof(*s->channel));
    return s;
}
unsigned sauti_aac_he_multi_channels(const SautiAacHeMulti *s){return s?s->channels:0u;}
unsigned sauti_aac_he_multi_frame_samples(const SautiAacHeMulti *s){return s?(s->downsampled?1024u:2048u):0u;}

int sauti_aac_he_multi_decode(SautiAacHeMulti *s,const uint8_t *au,size_t length,float *pcm,SautiAacReason *reason){
    SautiAacReason local;
    if(!reason)reason=&local;
    if(!s||!au||!pcm)return 0;
    const unsigned ch=s->channels,out_n=s->downsampled?1024u:2048u,rate=s->core_rate*2u;
    int status=0;
    float *core=(float *)malloc(sizeof(float)*1024u*ch);                       /* interleaved, output order */
    float *plane=(float *)malloc(sizeof(float)*1024u*ch);                      /* by slot */
    float *grid_out=(float *)malloc(sizeof(float)*2048u*ch);                   /* by slot */
    SautiAacSbrComplex *grid=(SautiAacSbrComplex *)malloc(sizeof(SautiAacSbrComplex)*SAUTI_AAC_SBR_CHANNEL_GRID);
    SautiAacLcwSbr *sb=(SautiAacLcwSbr *)malloc(sizeof(*sb));
    if(!core||!plane||!grid_out||!grid||!sb)goto done;
    status=sauti_aac_lcw_decode_sbr(s->lcw,au,length,core,reason,sb);
    if(status!=1)goto done;
    status=0;
    for(unsigned c=0;c<ch;c++){
        const unsigned slot=s->cfg.layout.map[c];
        for(unsigned i=0;i<1024u;i++)plane[(size_t)slot*1024u+i]=core[(size_t)i*ch+c];
    }
    for(unsigned e=0;e<s->nelem;e++){
        const Elem *el=&s->elem[e];
        const SautiAacLcwSbrItem *it=NULL;
        for(unsigned k=0;k<sb->count;k++)if(sb->item[k].elem_type==el->type&&sb->item[k].tag==el->tag&&sb->item[k].slot==el->slot){it=&sb->item[k];break;}
        const unsigned n=el->type==1u?2u:1u;
        SautiAacSbrPayloadCpeFrame cf;SautiAacSbrPayloadFrame mf;
        const SautiAacSbrPayloadFrame *fr[2]={NULL,NULL};
        if(it&&el->type==0u){
            if(!sauti_aac_sbr_payload_temporal_parse_ex(au,length,it->start_bit,it->bits,rate,&s->mono[e],&mf,NULL)){*reason=SAUTI_AAC_REASON_UNSUPPORTED_FIL;status=-1;goto done;}
            if(mf.has_ps){*reason=SAUTI_AAC_REASON_UNSUPPORTED_FIL;status=-1;goto done;}
            fr[0]=&mf;
        }else if(it&&el->type==1u){
            if(!sauti_aac_sbr_payload_cpe_parse(au,length,it->start_bit,it->bits,rate,&s->cpe[e],&cf)){*reason=SAUTI_AAC_REASON_UNSUPPORTED_FIL;status=-1;goto done;}
            fr[0]=&cf.channel[0];fr[1]=&cf.channel[1];
        }
        for(unsigned k=0;k<n;k++){
            const unsigned slot=el->slot+k;
            const float *cp=plane+(size_t)slot*1024u;
            const int ok=(fr[k]&&!fr[k]->no_sbr)?sauti_aac_sbr_channel_frame(&s->channel[slot],cp,fr[k],grid)
                                               :sauti_aac_sbr_channel_core_only(&s->channel[slot],cp,grid);
            if(!ok||!sauti_aac_sbr_channel_synthesize(&s->channel[slot],grid,grid_out+(size_t)slot*2048u)){*reason=SAUTI_AAC_REASON_INVALID_SYNTH;goto done;}
        }
    }
    for(unsigned c=0;c<ch;c++){
        const float *src=grid_out+(size_t)s->cfg.layout.map[c]*2048u;
        for(unsigned i=0;i<out_n;i++)pcm[(size_t)i*ch+c]=src[i];
    }
    *reason=SAUTI_AAC_REASON_OK;
    status=1;
done:
    free(core);free(plane);free(grid_out);free(grid);free(sb);
    return status;
}
