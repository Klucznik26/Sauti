/* AAC-LC / Main, any PCE layout, coupling channel elements: see sauti_aac_lcw.h.  Written from ISO/IEC 14496-3 (4.4.2 syntax, 4.6.8 coupling) and checked against FFmpeg on the ISO/IEC 14496-26
 * conformance streams (scripts/test_iso_conformance_vs_ffmpeg.py). */
#include "sauti_aac_lcw.h"
#include "sauti_aac_bands.h"
#include "sauti_aac_headers.h"
#include "sauti_aac_pred.h"
#include "sauti_aac_tables.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int get(const uint8_t *p,size_t n,size_t *pos,unsigned bits,uint32_t *v){
    return sauti_aac_bits_read_asm(p,n,pos,bits,v);
}
/* ---- PCE ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ */
/* Positions of the FFmpeg layout masks (the order of the output of a layout made of known positions). */
enum { POS_FL=0,POS_FR=1,POS_FC=2,POS_LFE=3,POS_BL=4,POS_BR=5,POS_FLC=6,POS_FRC=7,POS_BC=8,POS_SL=9,POS_SR=10,POS_NONE=255 };
int sauti_aac_lcw_pce_parse(const uint8_t *raw,size_t length,size_t *pos,SautiAacLcwLayout *out){
    uint32_t v,front,side,back,lfe,assoc,cc;
    if(!raw||!pos||!out)return 0;
    if(!get(raw,length,pos,4,&v)||!get(raw,length,pos,2,&v)||!get(raw,length,pos,4,&v)||
       !get(raw,length,pos,4,&front)||!get(raw,length,pos,4,&side)||!get(raw,length,pos,4,&back)||
       !get(raw,length,pos,2,&lfe)||!get(raw,length,pos,3,&assoc)||!get(raw,length,pos,4,&cc))return 0;
    for(unsigned k=0;k<2;k++){                                                 /* mono_mixdown, stereo_mixdown */
        if(!get(raw,length,pos,1,&v))return 0;
        if(v&&!get(raw,length,pos,4,&v))return 0;
    }
    if(!get(raw,length,pos,1,&v))return 0;                                     /* matrix_mixdown_idx_present */
    if(v&&(!get(raw,length,pos,2,&v)||!get(raw,length,pos,1,&v)))return 0;
    uint8_t cls[SAUTI_AAC_LCW_MAX_CHANNELS],cpe[SAUTI_AAC_LCW_MAX_CHANNELS],etag[SAUTI_AAC_LCW_MAX_CHANNELS];
    unsigned n=0,over=0;
    const uint32_t counts[3]={front,side,back};
    for(unsigned c=0;c<3;c++)for(uint32_t i=0;i<counts[c];i++){
        uint32_t is_cpe,tag;
        if(!get(raw,length,pos,1,&is_cpe)||!get(raw,length,pos,4,&tag))return 0;
        if(n>=SAUTI_AAC_LCW_MAX_CHANNELS){over=1;continue;}
        cls[n]=(uint8_t)c;cpe[n]=(uint8_t)is_cpe;etag[n]=(uint8_t)tag;n++;
    }
    for(uint32_t i=0;i<lfe;i++){uint32_t tag;if(!get(raw,length,pos,4,&tag))return 0;if(n>=SAUTI_AAC_LCW_MAX_CHANNELS){over=1;continue;}cls[n]=3;cpe[n]=0;etag[n]=(uint8_t)tag;n++;}
    for(uint32_t i=0;i<assoc;i++)if(!get(raw,length,pos,4,&v))return 0;
    for(uint32_t i=0;i<cc;i++)if(!get(raw,length,pos,1,&v)||!get(raw,length,pos,4,&v))return 0;
    while(*pos&7u)if(!get(raw,length,pos,1,&v))return 0;
    if(!get(raw,length,pos,8,&v))return 0;                                     /* comment_field_bytes */
    if(*pos>length*8u||v>(length*8u-*pos)/8u)return 0;
    *pos+=(size_t)v*8u;
    if(over||!n)return -1;
    SautiAacLcwLayout l;memset(&l,0,sizeof(l));
    unsigned channels=0,known=1;
    uint8_t position[SAUTI_AAC_LCW_MAX_CHANNELS+1];
    unsigned front_pairs=0,front_sce=0,back_pairs=0,back_sce=0,side_pairs=0,lfes=0,back_cpe_total=0;
    for(unsigned e=0;e<n;e++)if(cls[e]==2u&&cpe[e])back_cpe_total++;
    const unsigned second_back_pair=back_cpe_total==2u;
    for(unsigned e=0;e<n;e++){
        const unsigned need=cpe[e]?2u:1u;
        if(channels+need>SAUTI_AAC_LCW_MAX_CHANNELS)return -1;
        l.elem_slot[e]=(uint8_t)channels;
        uint8_t p0=POS_NONE,p1=POS_NONE;
        if(cls[e]==0){
            if(cpe[e]){ if(front_pairs<2u){p0=front_pairs?POS_FLC:POS_FL;p1=front_pairs?POS_FRC:POS_FR;front_pairs++;} }
            else if(!front_sce){p0=POS_FC;front_sce=1u;}
        }else if(cls[e]==1){
            if(cpe[e]&&!side_pairs){p0=POS_SL;p1=POS_SR;side_pairs=1u;}
        }else if(cls[e]==2){
            if(cpe[e]){
                if(back_pairs<2u&&!(back_pairs==1u&&side_pairs&&!second_back_pair)){
                    const int as_side=(back_pairs==0u&&!side_pairs&&second_back_pair);
                    p0=as_side?POS_SL:POS_BL;p1=as_side?POS_SR:POS_BR;back_pairs++;
                }
            }else if(!back_sce){p0=POS_BC;back_sce=1u;}
        }else if(!lfes){p0=POS_LFE;lfes=1u;}
        if(p0==POS_NONE)known=0;
        position[channels]=p0;if(need==2u)position[channels+1u]=p1;
        channels+=need;
    }
    l.channels=channels;l.n_elements=n;l.n_cce=cc;
    for(unsigned e=0;e<n;e++){l.elem_type[e]=cls[e]==3u?3u:(cpe[e]?1u:0u);l.elem_tag[e]=etag[e];}
    for(unsigned a=0;a<n;a++)for(unsigned b=a+1u;b<n;b++)                       /* a repeated (type, tag): the elements cannot be told apart by tag, the stream order decides */
        if(l.elem_type[a]==l.elem_type[b]&&l.elem_tag[a]==l.elem_tag[b])l.n_elements=0;
    unsigned o=0;
    if(known){
        for(unsigned bit=0;bit<=POS_SR;bit++)for(unsigned c=0;c<channels;c++)if(position[c]==bit)l.map[o++]=(uint8_t)c;
    }
    if(!known||o!=channels)for(o=0;o<channels;o++)l.map[o]=(uint8_t)o;           /* no standard positions: the order of the PCE list */
    *out=l;
    return 1;
}
/* ---- state ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- */
struct SautiAacLcw {
    SautiAacLcwConfig cfg;
    unsigned idx,n;
    uint32_t pns_seed;
    SautiAacLcSynthesis cce_syn[SAUTI_AAC_LCW_MAX_CCE];
    unsigned char cce_used[SAUTI_AAC_LCW_MAX_CCE];
    SautiAacLcSynthesis *syn;                                                  /* [channels] */
    SautiAacPredState *pred;                                                   /* [channels], Main only */
};
static size_t state_size(const SautiAacLcwConfig *c){
    return sizeof(SautiAacLcw)+(size_t)c->layout.channels*sizeof(SautiAacLcSynthesis)+(c->main?(size_t)c->layout.channels*sizeof(SautiAacPredState):0u);
}
static void state_links(SautiAacLcw *s){
    s->syn=(SautiAacLcSynthesis *)(s+1);
    s->pred=s->cfg.main?(SautiAacPredState *)(s->syn+s->cfg.layout.channels):NULL;
}
SautiAacLcw *sauti_aac_lcw_new(const SautiAacLcwConfig *config){
    if(!config||config->layout.channels<1u||config->layout.channels>SAUTI_AAC_LCW_MAX_CHANNELS||config->rate_index>(config->main?11u:12u)||(config->main&&config->frame960&&0))return NULL;
    SautiAacLcw *s=(SautiAacLcw *)calloc(1,state_size(config));
    if(!s)return NULL;
    s->cfg=*config;state_links(s);
    const unsigned layout=config->frame960?SAUTI_AAC_LAYOUT_960:SAUTI_AAC_LAYOUT_1024;
    s->idx=SAUTI_AAC_IDX(config->rate_index,layout)|(config->main?SAUTI_AAC_IDX_MAIN_FLAG:0u);
    s->n=config->frame960?960u:1024u;
    for(unsigned c=0;c<config->layout.channels;c++){
        (void)sauti_aac_lc_synthesis_init(&s->syn[c],0u);
        if(s->pred)sauti_aac_pred_reset(&s->pred[c]);
    }
    for(unsigned c=0;c<SAUTI_AAC_LCW_MAX_CCE;c++)(void)sauti_aac_lc_synthesis_init(&s->cce_syn[c],0u);
    s->pns_seed=s->syn[0].pns_seed;
    return s;
}
SautiAacLcw *sauti_aac_lcw_dup(const SautiAacLcw *state){
    if(!state)return NULL;
    SautiAacLcw *s=(SautiAacLcw *)malloc(state_size(&state->cfg));
    if(!s)return NULL;
    memcpy(s,state,state_size(&state->cfg));
    state_links(s);
    return s;
}
void sauti_aac_lcw_free(SautiAacLcw *state){free(state);}
unsigned sauti_aac_lcw_frame_samples(const SautiAacLcw *state){return state?state->n:0u;}
unsigned sauti_aac_lcw_channels(const SautiAacLcw *state){return state?state->cfg.layout.channels:0u;}
/* ---- decoding ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ */
static int skip_dse(const uint8_t *raw,size_t length,size_t *pos){
    uint32_t tag,align,count,esc=0;
    if(!get(raw,length,pos,4,&tag)||!get(raw,length,pos,1,&align)||!get(raw,length,pos,8,&count))return 0;
    if(count==255u){if(!get(raw,length,pos,8,&esc))return 0;count+=esc;}
    if(align)*pos=(*pos+7u)&~(size_t)7u;
    if(*pos>length*8u||(size_t)count*8u>length*8u-*pos)return 0;
    *pos+=(size_t)count*8u;
    return 1;
}
typedef struct { uint8_t type,tag,slot,present; } Seen;                          /* the channel elements of this frame: (type, tag) -> first slot, for the targets of coupling */
typedef struct {
    unsigned tag,point,num_coupled,num_gain;                                      /* point: 0 before TNS, 1 between TNS and the filter bank, 3 after (independent) */
    uint8_t type[8],id[8],sel[8];
    float gain[16][SAUTI_AAC_SF_GROUPS*SAUTI_AAC_SF_BANDS];
    float spectral[1024],time[1024];
    SautiAacLcnStage st;
} Cce;
static const float cce_scale[4]={1.0905077326652577f,1.1892071150027210f,1.4142135623730951f,2.0f};
static int cce_parse(SautiAacLcw *s,const uint8_t *au,size_t length,size_t *pos,uint32_t *seed,Cce *c,SautiAacReason *reason){
    uint32_t v,sign,scale,domain;
    memset(c,0,sizeof(*c));
    if(!get(au,length,pos,4,&v))return 0;
    c->tag=v;
    if(!get(au,length,pos,1,&v))return 0;
    c->point=2u*v;
    if(!get(au,length,pos,3,&v))return 0;
    c->num_coupled=v;
    c->num_gain=0;
    for(unsigned i=0;i<=c->num_coupled;i++){
        uint32_t is_cpe,id,sel=2;
        c->num_gain++;
        if(!get(au,length,pos,1,&is_cpe)||!get(au,length,pos,4,&id))return 0;
        if(is_cpe){if(!get(au,length,pos,2,&sel))return 0;if(sel==3u)c->num_gain++;}
        c->type[i]=(uint8_t)(is_cpe?1u:0u);c->id[i]=(uint8_t)id;c->sel[i]=(uint8_t)sel;
    }
    if(!get(au,length,pos,1,&domain))return 0;
    c->point+=(domain||c->point==2u)?1u:0u;
    if(!get(au,length,pos,1,&sign)||!get(au,length,pos,2,&scale))return 0;
    int st=sauti_aac_lcn_stage_cce(s->idx,au,length,pos,seed,c->spectral,&c->st,reason);
    if(st!=1)return st;
    SautiAacHuffNode nodes[512];
    const size_t node_count=sauti_aac_standard_tree_build(0,nodes,sizeof(nodes)/sizeof(nodes[0]));
    if(!node_count)return 0;
    const SautiAacIcsInfo *ics=&c->st.ics;
    for(unsigned l=0;l<c->num_gain;l++){
        int gain=0;unsigned cge=1;float cache=1.0f;
        if(l){
            int32_t d=0;
            if(c->point==3u)cge=1u;
            else{uint32_t b;if(!get(au,length,pos,1,&b))return 0;cge=b;}
            if(cge){if(!sauti_aac_scalefactor_symbol_decode(au,length,pos,nodes,node_count,&d))return 0;gain=d;}
            cache=powf(cce_scale[scale],(float)-gain);
        }
        if(c->point==3u){c->gain[l][0]=cache;continue;}
        unsigned idx=0;
        for(unsigned g=0;g<ics->group_count;g++)for(unsigned b=0;b<ics->max_sfb;b++,idx++){
            if(c->st.band_zero[g][b])continue;
            if(!cge){
                int32_t t=0;
                if(!sauti_aac_scalefactor_symbol_decode(au,length,pos,nodes,node_count,&t))return 0;
                if(t){
                    int sg=1;
                    gain+=t;t=gain;
                    if(sign){sg-=2*(t&1);t>>=1;}
                    cache=powf(cce_scale[scale],(float)-t)*(float)sg;
                }
            }
            if(idx<SAUTI_AAC_SF_GROUPS*SAUTI_AAC_SF_BANDS)c->gain[l][idx]=cache;
        }
    }
    return 1;
}
/* dependent coupling: dest += gain * source in the bands the coupling channel carries */
static void couple_dependent(unsigned frame_idx,unsigned n,float *dest,const Cce *c,unsigned list){
    const SautiAacIcsInfo *ics=&c->st.ics;
    const uint16_t *offsets=NULL;unsigned bands=0;
    if(!sauti_aac_band_offsets(frame_idx,ics->window_sequence==2u,&offsets,&bands))return;
    const unsigned ns=n/8u;
    const float *src=c->spectral;
    unsigned idx=0;
    for(unsigned g=0;g<ics->group_count;g++){
        for(unsigned b=0;b<ics->max_sfb&&b<bands;b++,idx++){
            if(c->st.band_zero[g][b])continue;
            const float gain=c->gain[list][idx];
            for(unsigned w=0;w<ics->group_length[g];w++)
                for(unsigned k=offsets[b];k<offsets[b+1u];k++)dest[w*ns+k]+=gain*src[w*ns+k];
        }
        idx+=0;
        dest+=ics->group_length[g]*ns;src+=ics->group_length[g]*ns;
    }
}
/* the targets of one coupling channel element, FFmpeg's order of gain lists */
static void for_targets(const Cce *c,const Seen *seen,unsigned nseen,unsigned point,float *spectral,float *time,SautiAacLcw *s){
    (void)point;
    unsigned index=0;
    for(unsigned i=0;i<=c->num_coupled;i++){
        const Seen *t=NULL;
        for(unsigned k=0;k<nseen;k++)if(seen[k].present&&seen[k].type==c->type[i]&&seen[k].tag==c->id[i]){t=&seen[k];break;}
        if(t){
            const unsigned sel=c->sel[i];
            if(sel!=1u){
                if(c->point==3u){float *o=time+(size_t)t->slot*1024u;const float g=c->gain[index][0];for(unsigned x=0;x<s->n;x++)o[x]+=g*c->time[x];}
                else couple_dependent(s->idx,s->n,spectral+(size_t)t->slot*1024u,c,index);
                if(sel!=0u)index++;
            }
            if(sel!=2u){
                if(c->point==3u){float *o=time+(size_t)(t->slot+1u)*1024u;const float g=c->gain[index][0];for(unsigned x=0;x<s->n;x++)o[x]+=g*c->time[x];}
                else couple_dependent(s->idx,s->n,spectral+(size_t)(t->slot+1u)*1024u,c,index);
                index++;
            }
        }else index+=1u+(c->sel[i]==3u);
    }
}
int sauti_aac_lcw_decode(SautiAacLcw *state,const uint8_t *au,size_t length,float *pcm,SautiAacReason *reason){
    SautiAacReason local=SAUTI_AAC_REASON_INVALID_SCE;
    if(!reason)reason=&local;
    *reason=SAUTI_AAC_REASON_INVALID_SCE;
    if(!state||!au||!pcm||length>SIZE_MAX/8u)return 0;
    const SautiAacLcwLayout *lay=&state->cfg.layout;
    const unsigned channels=lay->channels,n=state->n;
    SautiAacLcw *next=sauti_aac_lcw_dup(state);
    float *spectral=(float *)calloc((size_t)channels*1024u,sizeof(float));
    float *out=(float *)calloc((size_t)channels*1024u,sizeof(float));
    SautiAacLcnStage *stage=(SautiAacLcnStage *)calloc(channels,sizeof(*stage));
    Seen *seen=(Seen *)calloc(channels,sizeof(*seen));
    Cce *cce=(Cce *)calloc(SAUTI_AAC_LCW_MAX_CCE,sizeof(*cce));
    unsigned char *cce_present=(unsigned char *)calloc(SAUTI_AAC_LCW_MAX_CCE,1);
    unsigned char *slot_used=(unsigned char *)calloc(channels,1);
    int status=0;
    if(!next||!spectral||!out||!stage||!seen||!cce||!cce_present||!slot_used){status=0;goto done;}
    {
    size_t pos=0;unsigned used=0,nseen=0,ncce=0;uint32_t seed=next->pns_seed;
    for(;;){
        size_t at=pos;uint32_t id=0;
        if(!get(au,length,&pos,3,&id)){*reason=SAUTI_AAC_REASON_INVALID_TRAILING;goto fail0;}
        if(id==7u)break;
        if(id==4u){if(!skip_dse(au,length,&pos)){*reason=SAUTI_AAC_REASON_INVALID_TRAILING;goto fail0;}continue;}
        if(id==6u){
            const int sk=sauti_aac_fill_skip_non_sbr(au,length,&pos,NULL);
            if(sk==-1){*reason=SAUTI_AAC_REASON_UNSUPPORTED_FIL;status=-1;goto done;}
            if(sk!=1){*reason=SAUTI_AAC_REASON_INVALID_TRAILING;goto fail0;}
            continue;
        }
        if(id==5u){SautiAacLcwLayout dummy;if(sauti_aac_lcw_pce_parse(au,length,&pos,&dummy)<0||pos>length*8u){*reason=SAUTI_AAC_REASON_INVALID_TRAILING;goto fail0;}continue;}
        if(id==2u){
            if(ncce>=SAUTI_AAC_LCW_MAX_CCE){*reason=SAUTI_AAC_REASON_UNSUPPORTED_ELEMENT;status=-1;goto done;}
            Cce *c=&cce[ncce];
            int r=cce_parse(next,au,length,&pos,&seed,c,reason);
            if(r!=1){status=r;goto done;}
            if(c->tag>=SAUTI_AAC_LCW_MAX_CCE||cce_present[c->tag]){*reason=SAUTI_AAC_REASON_UNSUPPORTED_ELEMENT;goto fail0;}
            cce_present[c->tag]=1u;ncce++;
            continue;
        }
        if(id!=0u&&id!=1u&&id!=3u){*reason=SAUTI_AAC_REASON_UNSUPPORTED_ELEMENT;status=-1;goto done;}
        {
            const unsigned need=id==1u?2u:1u;
            uint32_t tag=0;size_t tp=at+3u;
            if(!get(au,length,&tp,4,&tag)){*reason=SAUTI_AAC_REASON_INVALID_TRAILING;goto fail0;}
            unsigned slot=channels;
            if(lay->n_elements){
                for(unsigned e=0;e<lay->n_elements;e++)if(lay->elem_type[e]==(id==3u?3u:id)&&lay->elem_tag[e]==tag){slot=lay->elem_slot[e];break;}
            }else slot=used;
            if(slot>=channels||slot+need>channels||slot_used[slot]||(need==2u&&slot_used[slot+1u])){*reason=SAUTI_AAC_REASON_UNSUPPORTED_ELEMENT;goto fail0;}
            SautiAacLcnConfig cfg;memset(&cfg,0,sizeof(cfg));
            cfg.rate_index=next->cfg.rate_index;cfg.main=next->cfg.main;cfg.frame960=next->cfg.frame960;cfg.channel_config=id==1u?2u:1u;
            SautiAacLcnChannel ch[2];
            for(unsigned k=0;k<need;k++){ch[k].syn=&next->syn[slot+k];ch[k].pred=next->pred?&next->pred[slot+k]:NULL;ch[k].ltp=NULL;ch[k].ssr=NULL;}
            if(need==1u)ch[1]=ch[0];
            float (*sp)[1024]=(float (*)[1024])(spectral+(size_t)slot*1024u);
            int r=sauti_aac_lcn_stage_element(&cfg,next->idx,id,au,length,at,&pos,ch,&seed,sp,&stage[slot],reason);
            if(r!=1){status=r;goto done;}
            for(unsigned k=0;k<need;k++)slot_used[slot+k]=1u;
            seen[nseen].type=(uint8_t)(id==1u?1u:0u);seen[nseen].tag=(uint8_t)tag;seen[nseen].slot=(uint8_t)slot;seen[nseen].present=(id!=3u);nseen++;
            used+=need;
        }
    }
    while(pos&7u){uint32_t pad=1;if(!get(au,length,&pos,1,&pad)){*reason=SAUTI_AAC_REASON_INVALID_TRAILING;goto fail0;}}
    if(pos!=length*8u||used!=channels){*reason=SAUTI_AAC_REASON_INVALID_TRAILING;goto fail0;}
    next->pns_seed=seed;
    /* The coupling channel's own TNS is applied before it is added (conformance streams al07: FFmpeg agrees at 138 dB); 'before TNS' / 'after TNS' refers to the target's TNS. */
    for(unsigned i=0;i<ncce;i++)if(!sauti_aac_lcn_stage_tns(&cce[i].st,next->idx,cce[i].spectral)){*reason=SAUTI_AAC_REASON_INVALID_SYNTH;goto fail0;}
    for(unsigned i=0;i<ncce;i++)if(cce[i].point==0u)for_targets(&cce[i],seen,nseen,0u,spectral,NULL,next);
    for(unsigned c=0;c<channels;c++)if(!sauti_aac_lcn_stage_tns(&stage[c],next->idx,spectral+(size_t)c*1024u)){*reason=SAUTI_AAC_REASON_INVALID_SYNTH;goto fail0;}
    for(unsigned i=0;i<ncce;i++)if(cce[i].point==1u)for_targets(&cce[i],seen,nseen,1u,spectral,NULL,next);
    for(unsigned c=0;c<channels;c++)
        if(!sauti_aac_lcn_stage_synth(next->idx,&next->syn[c],&stage[c],spectral+(size_t)c*1024u,out+(size_t)c*1024u)){*reason=SAUTI_AAC_REASON_INVALID_SYNTH;goto fail0;}
    for(unsigned i=0;i<ncce;i++)if(cce[i].point==3u){                                /* independent coupling: the coupling channel through its own filter bank, added in the time domain */
        if(!sauti_aac_lcn_stage_synth(next->idx,&next->cce_syn[cce[i].tag],&cce[i].st,cce[i].spectral,cce[i].time)){*reason=SAUTI_AAC_REASON_INVALID_SYNTH;goto fail0;}
        for_targets(&cce[i],seen,nseen,3u,NULL,out,next);
    }
    float *buf=(float *)malloc(sizeof(float)*n*channels);
    if(!buf){status=0;goto done;}
    for(unsigned c=0;c<channels;c++){
        const float *o=out+(size_t)lay->map[c]*1024u;
        for(unsigned i=0;i<n;i++){
            const float v=ldexpf(o[i],-16);
            if(!isfinite(v)){free(buf);*reason=SAUTI_AAC_REASON_INVALID_SYNTH;goto fail0;}
            buf[(size_t)i*channels+c]=v;
        }
    }
    memcpy(state,next,state_size(&state->cfg));
    state_links(state);
    memcpy(pcm,buf,sizeof(float)*n*channels);
    free(buf);
    *reason=SAUTI_AAC_REASON_OK;
    status=1;
    goto done;
    }
fail0:
    status=0;
done:
    free(next);free(spectral);free(out);free(stage);free(seen);free(cce);free(cce_present);free(slot_used);
    return status;
}
