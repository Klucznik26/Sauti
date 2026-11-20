/* AAC-LD and AAC-ELD decoder (ISO/IEC 14496-3 error-resilient profiles with epConfig 0 and no resilience tools), see sauti_aac_er.h.
 * The bitstream tools (sections, scalefactors, Huffman spectral data, pulse, TNS, PNS, Mid/Side, intensity) are the AAC-LC ones, with the 512 / 480 line scalefactor-band tables;
 * only the element syntax, the transform and the window differ.  No FFmpeg or FAAD2 code is used; the numeric tables are listed in sauti_aac_bands.c and sauti_aac_eld_window.c. */
#include "sauti_aac_er.h"
#include "sauti_aac_bands.h"
#include "sauti_aac_dequant.h"
#include "sauti_aac_eld_window.h"
#include "sauti_aac_headers.h"
#include "sauti_aac_imdct.h"
#include "sauti_aac_pns.h"
#include "sauti_aac_spectral_data.h"
#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

static int get(const uint8_t *data,size_t length,size_t *pos,unsigned width,uint32_t *value){
    return sauti_aac_bits_read_asm(data,length,pos,width,value);
}

unsigned sauti_aac_er_frame_samples(const SautiAacErConfig *c){
    if(!c || (c->aot!=SAUTI_AAC_AOT_ER_LD && c->aot!=SAUTI_AAC_AOT_ER_ELD) || c->rate_index<3u || c->rate_index>7u ||
       c->channel_config<1u || c->channel_config>2u || c->frame480>1u)return 0u;
    return c->frame480?480u:512u;
}
void sauti_aac_er_state_init(SautiAacErState *state){
    memset(state,0,sizeof(*state));
    state->pns_seed=0x1f2e3d4cu;           /* the same generator seed as the AAC-LC decoders (sauti_aac_lc_synthesis_init) */
    for(unsigned c=0;c<2;c++)state->channel[c].initialized=1u;
}

/* ---- AudioSpecificConfig ---------------------------------------------------------------------------------------------------------------------------------------- */
int sauti_aac_er_asc_parse(const uint8_t *asc,size_t length,SautiAacErAsc *out){
    size_t pos=0;
    uint32_t aot=0,sfi=0,chan=0,v=0,ep=0;
    SautiAacErAsc a;memset(&a,0,sizeof(a));
    if(!asc || !out || length==0u || length>SIZE_MAX/8u)return 0;
    if(!get(asc,length,&pos,5,&aot))return 0;
    if(aot==31u){if(!get(asc,length,&pos,6,&v))return 0;aot=32u+v;}
    if(!get(asc,length,&pos,4,&sfi))return 0;
    if(sfi==15u)return -1;                                                    /* explicit 24-bit sampling frequency */
    if(sfi>12u)return 0;
    if(!get(asc,length,&pos,4,&chan))return 0;
    if(aot!=SAUTI_AAC_AOT_ER_LD && aot!=SAUTI_AAC_AOT_ER_ELD)return -1;
    a.aot=aot;a.rate_index=sfi;a.channel_config=chan;
    if(aot==SAUTI_AAC_AOT_ER_LD){
        uint32_t frame_flag=0,core=0,ext=0;
        if(!get(asc,length,&pos,1,&frame_flag) || !get(asc,length,&pos,1,&core) || !get(asc,length,&pos,1,&ext))return 0;   /* GASpecificConfig */
        if(core)return -1;                                                    /* dependsOnCoreCoder: scalable AAC, not supported */
        if(chan==0u)return -1;                                                /* program_config_element */
        if(ext){
            uint32_t res=0,ext3=0;
            if(!get(asc,length,&pos,3,&res) || !get(asc,length,&pos,1,&ext3))return 0;
            if(res)return -1;                                                 /* error-resilience tools (HCR / RVLC / VCB11) */
        }
        a.frame480=frame_flag;
    }else{
        uint32_t frame_flag=0,r1=0,r2=0,r3=0,sbr=0;
        if(!get(asc,length,&pos,1,&frame_flag) || !get(asc,length,&pos,1,&r1) || !get(asc,length,&pos,1,&r2) || !get(asc,length,&pos,1,&r3) ||
           !get(asc,length,&pos,1,&sbr))return 0;                              /* ELDSpecificConfig */
        if(r1 || r2 || r3)return -1;
        a.frame480=frame_flag;a.ld_sbr=sbr;
        if(sbr){
            uint32_t rate=0,crc=0;
            if(!get(asc,length,&pos,1,&rate) || !get(asc,length,&pos,1,&crc))return 0;
            a.ld_sbr_downsampled=rate;a.ld_sbr_crc=crc;
            /* the ld_sbr_header()s follow: they are parsed by the SBR decoder from the first frames' data; here we only need to find the end of the config */
        }
        if(!sbr){
            for(;;){                                                          /* ELD extension loop: types other than ELDEXT_TERM (0) carry a length and are skipped */
                uint32_t type=0,len=0,add=0;
                if(!get(asc,length,&pos,4,&type))return 0;
                if(type==0u)break;
                if(!get(asc,length,&pos,4,&len))return 0;
                if(len==15u){if(!get(asc,length,&pos,8,&add))return 0;len+=add;if(add==255u){if(!get(asc,length,&pos,16,&add))return 0;len+=add-15u;}}
                for(uint32_t i=0;i<len;i++)if(!get(asc,length,&pos,8,&v))return 0;
            }
        }
    }
    if(aot==SAUTI_AAC_AOT_ER_LD || !a.ld_sbr){
        if(!get(asc,length,&pos,2,&ep))return 0;                              /* epConfig */
        if(ep)return -1;
    }
    a.bits=(unsigned)pos;
    *out=a;
    return 1;
}

/* ---- windows ---------------------------------------------------------------------------------------------------------------------------------------------------- */
/* AAC-LD window of length 2n: shape 0 = sine, full overlap: w[i] = sin(pi (i + 1/2) / (2n)); shape 1 = low overlap (n = 512 only): zeros, a 128-sample sine slope, ones,
 * mirrored in the second half: w = 0 (i < 192), sin(pi (i - 192 + 1/2) / 256) (192 <= i < 320), 1 (320 <= i < 512). */
typedef struct { float w[1024]; } LdWindow;
static LdWindow ld_windows[2][2];                /* [n == 480][shape] */
static atomic_int ld_window_state[2][2];         /* 0 = not built, 1 = being built, 2 = ready */
static const float *ld_window(unsigned n,unsigned shape){
    const unsigned a=n==480u,b=shape;
    LdWindow *lw=&ld_windows[a][b];
    int expected=0;
    if(atomic_compare_exchange_strong(&ld_window_state[a][b],&expected,1)){
        const double pi=3.14159265358979323846264338327950288;
        for(unsigned i=0;i<n;i++){
            double v;
            if(shape==0u)v=sin(pi*((double)i+0.5)/(2.0*(double)n));
            else if(i<192u)v=0.0;
            else if(i<320u)v=sin(pi*((double)(i-192u)+0.5)/256.0);
            else v=1.0;
            lw->w[i]=(float)v;
            lw->w[2u*n-1u-i]=(float)v;
        }
        atomic_store(&ld_window_state[a][b],2);
    }else while(atomic_load(&ld_window_state[a][b])!=2){}
    return lw->w;
}

/* AAC-LD synthesis of one channel from its dequantised spectrum (TNS already inverted by the caller): IMDCT of n lines, window, overlap-add with the previous frame's second half. */
static int ld_synthesis(SautiAacErChannel *ch,unsigned n,unsigned shape,const float spectral[1024],float *pcm){
    float time[2048];
    if(shape>1u || (shape==1u && n!=512u))return 0;               /* the low-overlap window is defined for 512-line frames only */
    if(!sauti_aac_imdct(spectral,n,time))return 0;
    const float *w=ld_window(n,shape);
    float next[512];
    for(unsigned i=0;i<n;i++){
        pcm[i]=ch->overlap[i]+time[i]*w[i];
        next[i]=time[n+i]*w[n+i];
        if(!isfinite(pcm[i]))return 0;
    }
    memcpy(ch->overlap,next,n*sizeof(float));
    return 1;
}

/* AAC-ELD low-delay filterbank of one channel (N = 512 or 480).  The spectrum is rearranged so that the low-delay MDCT becomes the conventional IMDCT of N lines (the mapping of
 * Chivukula, Reznik, Devarajan, ICALIP 2008), the middle N samples of that IMDCT are sign-alternated and then combined with the three previous frames through the 3.75 N window; the
 * history keeps the last three transform outputs.  The scaling is the same as in the AAC-LD path (the caller multiplies by 2^-16 at the end). */
static int eld_synthesis(SautiAacErChannel *ch,unsigned n,const float spectral[1024],float *pcm){
    const float *window=n==480u?sauti_aac_eld_window_480:sauti_aac_eld_window_512;
    const unsigned n2=n>>1,n4=n>>2;
    float in[512],y[1024],buf[512],next[1536];
    memcpy(in,spectral,n*sizeof(float));
    for(unsigned i=0;i<n2;i+=2){
        float temp=in[i];in[i]=-in[n-1u-i];in[n-1u-i]=temp;
        temp=-in[i+1u];in[i+1u]=in[n-2u-i];in[n-2u-i]=temp;
    }
    if(!sauti_aac_imdct(in,n,y))return 0;
    for(unsigned j=0;j<n;j++)buf[j]=y[n2+j];
    for(unsigned i=0;i<n;i+=2){buf[i]=-buf[i];}
    const float *saved=ch->overlap;
    for(unsigned i=n4;i<n2;i++)
        pcm[i-n4]=buf[n2-1u-i]*window[i-n4]+saved[i+n2]*window[i+n-n4]-saved[n+n2-1u-i]*window[i+2u*n-n4]-saved[2u*n+n2+i]*window[i+3u*n-n4];
    for(unsigned i=0;i<n2;i++)
        pcm[n4+i]=buf[i]*window[i+n2-n4]-saved[n-1u-i]*window[i+n2+n-n4]-saved[n+i]*window[i+n2+2u*n-n4]+saved[2u*n+n-1u-i]*window[i+n2+3u*n-n4];
    for(unsigned i=0;i<n4;i++)
        pcm[n2+n4+i]=buf[i+n2]*window[i+n-n4]-saved[n2-1u-i]*window[i+2u*n-n4]-saved[n+n2+i]*window[i+3u*n-n4];
    for(unsigned i=0;i<n;i++)if(!isfinite(pcm[i]))return 0;
    memcpy(next,buf,n*sizeof(float));                       /* history: newest transform output first, then the two before it */
    memcpy(next+n,saved,2u*n*sizeof(float));
    memcpy(ch->overlap,next,3u*n*sizeof(float));
    return 1;
}

/* ---- element syntax --------------------------------------------------------------------------------------------------------------------------------------------- */
static int eld_ics_info(const uint8_t *data,size_t length,size_t *pos,SautiAacIcsInfo *ics){
    uint32_t max_sfb=0;
    if(!get(data,length,pos,6,&max_sfb))return 0;
    memset(ics,0,sizeof(*ics));
    ics->max_sfb=max_sfb;ics->window_count=1;ics->group_count=1;ics->group_length[0]=1;
    return 1;
}
static int ics_info(unsigned aot,const uint8_t *data,size_t length,size_t *pos,SautiAacIcsInfo *ics){
    if(aot==SAUTI_AAC_AOT_ER_ELD)return eld_ics_info(data,length,pos,ics);
    if(!sauti_aac_ics_info_parse(data,length,pos,ics))return 0;
    return ics->window_sequence==0u && ics->window_count==1u;      /* AAC-LD is defined for ONLY_LONG_SEQUENCE only */
}
/* one individual_channel_stream() of the ER syntax: 1 ok, 0 malformed, -1 unsupported tool */
static int channel_parse(unsigned aot,unsigned idx,const uint8_t *data,size_t length,size_t *pos,const SautiAacIcsInfo *common,
                         SautiAacLcMonoData *ch,SautiAacReason *reason,unsigned allow_intensity){
    uint32_t v=0;
    if(!get(data,length,pos,8,&ch->global_gain))return 0;
    if(common)ch->ics=*common;
    else if(!ics_info(aot,data,length,pos,&ch->ics))return 0;
    const uint16_t *offsets=NULL;unsigned bands=0;
    if(!sauti_aac_band_offsets(idx,0u,&offsets,&bands) || ch->ics.max_sfb>bands)return 0;
    if(!sauti_aac_lc_sections_parse(data,length,pos,&ch->ics,&ch->sections))return 0;
    if(!sauti_aac_lc_scalefactors_parse(data,length,pos,ch->global_gain,&ch->sections,&ch->factors)){
        *reason=SAUTI_AAC_REASON_INVALID_SCALEFACTORS;return 0;
    }
    SautiAacPulseData pulse;memset(&pulse,0,sizeof(pulse));
    unsigned have_pulse=0;
    if(aot==SAUTI_AAC_AOT_ER_LD){
        if(!get(data,length,pos,1,&v))return 0;
        if(v){
            if(!sauti_aac_pulse_parse(data,length,pos,&ch->ics,&pulse)){*reason=SAUTI_AAC_REASON_INVALID_SCE;return 0;}
            have_pulse=1u;
        }
    }
    uint32_t tns_present=0;
    if(!get(data,length,pos,1,&tns_present))return 0;
    if(aot==SAUTI_AAC_AOT_ER_LD){
        if(!get(data,length,pos,1,&v))return 0;
        if(v){*reason=SAUTI_AAC_REASON_GAIN_CONTROL;return -1;}
    }
    if(tns_present){                                               /* in the ER syntax the TNS data follow the gain-control bit */
        ch->tns_present=1u;
        if(!sauti_aac_tns_parse(data,length,pos,&ch->ics,&ch->tns))return 0;
    }
    for(unsigned i=0;i<ch->sections.section_count;i++)
        if(ch->sections.sections[i].codebook>=14u && !allow_intensity){*reason=SAUTI_AAC_REASON_INTENSITY;return -1;}
    if(!sauti_aac_lc_spectral_data_parse(data,length,pos,idx,&ch->ics,&ch->sections,ch->quant)){
        *reason=SAUTI_AAC_REASON_INVALID_SPECTRAL;return 0;
    }
    if(have_pulse && !sauti_aac_lc_pulse_apply(ch->quant,idx,&ch->ics,&ch->factors,&pulse)){
        *reason=SAUTI_AAC_REASON_INVALID_SPECTRAL;return 0;
    }
    return 1;
}
static int cpe_parse(unsigned aot,unsigned idx,const uint8_t *data,size_t length,size_t *pos,SautiAacCpeData *out,SautiAacReason *reason){
    uint32_t tag=0,common=1,mask=0;
    SautiAacCpeData parsed;memset(&parsed,0,sizeof(parsed));
    SautiAacIcsInfo shared;memset(&shared,0,sizeof(shared));
    if(aot==SAUTI_AAC_AOT_ER_LD){
        if(!get(data,length,pos,4,&tag) || !get(data,length,pos,1,&common))return 0;
    }
    parsed.common_window=common;
    if(common){
        if(!ics_info(aot,data,length,pos,&shared))return 0;
        if(!get(data,length,pos,2,&mask) || mask==3u)return 0;
        parsed.ms_mask_present=mask;
        if(mask==1u || mask==2u)
            for(unsigned sfb=0;sfb<shared.max_sfb;sfb++){
                uint32_t used=mask==2u?1u:0u;
                if(mask==1u && !get(data,length,pos,1,&used))return 0;
                parsed.ms_used[0][sfb]=(uint8_t)used;
            }
    }
    for(unsigned c=0;c<2;c++){
        const int status=channel_parse(aot,idx,data,length,pos,common?&shared:NULL,&parsed.channel[c],reason,common && c==1u);
        if(status!=1)return status;
    }
    *out=parsed;
    return 1;
}

/* ---- frame ------------------------------------------------------------------------------------------------------------------------------------------------------- */
static int synthesize_channel(const SautiAacErConfig *cfg,unsigned idx,SautiAacErChannel *state,const SautiAacLcMonoData *data,float spectral[1024],float *pcm){
    const unsigned n=cfg->frame480?480u:512u;
    float s[1024];
    memcpy(s,spectral,sizeof(s));
    if(data->tns_present && !sauti_aac_tns_apply(&data->tns,&data->ics,idx,s))return 0;
    if(cfg->aot==SAUTI_AAC_AOT_ER_LD)return ld_synthesis(state,n,data->ics.window_shape,s,pcm);
    return eld_synthesis(state,n,s,pcm);
}
int sauti_aac_er_decode(const SautiAacErConfig *cfg,SautiAacErState *state,const uint8_t *au,size_t length,float *pcm,SautiAacReason *reason){
    SautiAacReason local=SAUTI_AAC_REASON_INVALID_SCE;
    if(!reason)reason=&local;
    *reason=SAUTI_AAC_REASON_INVALID_SCE;
    const unsigned n=sauti_aac_er_frame_samples(cfg);
    if(!n || !state || !au || !pcm || length>SIZE_MAX/8u)return 0;
    const unsigned idx=SAUTI_AAC_IDX(cfg->rate_index,cfg->frame480?SAUTI_AAC_LAYOUT_480:SAUTI_AAC_LAYOUT_512);
    SautiAacErState next=*state;
    size_t pos=0;
    float out[2][512];
    if(cfg->channel_config==1u){
        uint32_t tag=0;
        SautiAacLcMonoData *data=(SautiAacLcMonoData *)calloc(1,sizeof(*data));       /* ~9 KB: keep it off the stack with the CPE variant below */
        if(!data)return 0;
        int status=1;
        if(cfg->aot==SAUTI_AAC_AOT_ER_LD && !get(au,length,&pos,4,&tag))status=0;
        if(status==1)status=channel_parse(cfg->aot,idx,au,length,&pos,NULL,data,reason,0);
        if(status==1){
            float spectral[1024];
            if(!sauti_aac_lc_dequantize(data->quant,idx,&data->ics,&data->sections,&data->factors,spectral) ||
               !sauti_aac_pns_apply(&data->ics,idx,&data->sections,&data->factors,&next.pns_seed,spectral) ||
               !synthesize_channel(cfg,idx,&next.channel[0],data,spectral,out[0])){*reason=SAUTI_AAC_REASON_INVALID_SYNTH;status=0;}
        }
        free(data);
        if(status!=1)return status;
        for(unsigned i=0;i<n;i++){
            const float v=ldexpf(out[0][i],-16);
            if(!isfinite(v)){*reason=SAUTI_AAC_REASON_INVALID_SYNTH;return 0;}
            out[0][i]=v;
        }
        memcpy(pcm,out[0],n*sizeof(float));
    }else{
        SautiAacCpeData *cpe=(SautiAacCpeData *)calloc(1,sizeof(*cpe));
        if(!cpe)return 0;
        int status=cpe_parse(cfg->aot,idx,au,length,&pos,cpe,reason);
        if(status==1){
            float spectral[2][1024];
            if(sauti_aac_cpe_spectra(cpe,idx,&next.pns_seed,spectral,reason)!=1)status=0;
            else for(unsigned c=0;c<2 && status==1;c++)
                if(!synthesize_channel(cfg,idx,&next.channel[c],&cpe->channel[c],spectral[c],out[c])){*reason=SAUTI_AAC_REASON_INVALID_SYNTH;status=0;}
        }
        free(cpe);
        if(status!=1)return status;
        for(unsigned i=0;i<n;i++){
            const float l=ldexpf(out[0][i],-16),r=ldexpf(out[1][i],-16);
            if(!isfinite(l) || !isfinite(r)){*reason=SAUTI_AAC_REASON_INVALID_SYNTH;return 0;}
            pcm[2u*i]=l;pcm[2u*i+1u]=r;
        }
    }
    *state=next;
    *reason=SAUTI_AAC_REASON_OK;
    return 1;
}
