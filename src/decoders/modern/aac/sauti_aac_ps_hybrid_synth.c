#include "sauti_aac_ps_hybrid_synth.h"
#include <math.h>
#include <stdint.h>
#include <string.h>

#define PS_SYNTH_SLOTS 32u
#define PS_SYNTH_QMF_BANDS 64u
static int disjoint(const void *a,size_t as,const void *b,size_t bs){
    const uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x<=y?y-x>=as:x-y>=bs;
}
static int validate_schedule(const SautiAacPsHybridSchedule *s){
    if(!s)return 0;
    const unsigned h=s->hybrid_bands;
    const unsigned p=s->parameter_bands;
    if(!((p==20u&&h==71u)||(p==34u&&h==91u))||
       !s->env_count||s->env_count>SAUTI_AAC_PS_MAX_ENVELOPES)
        return 0;
    for(unsigned slot=0;slot<PS_SYNTH_SLOTS;slot++)
        if(s->slot_to_env[slot]>=s->env_count)return 0;
    for(unsigned k=0;k<h;k++)
        if(s->hybrid_to_parameter[k]>=p)return 0;
    return 1;
}
static int validate_samples(const SautiAacSbrComplex *v,size_t n){
    for(size_t i=0;i<n;i++)
        if(!isfinite(v[i].re)||!isfinite(v[i].im))return 0;
    return 1;
}
static int synth_raw(const SautiAacSbrComplex *hybrid,unsigned bands,
                     SautiAacSbrComplex *qmf){
    static const unsigned split20[3]={6u,2u,2u};
    static const unsigned split34[5]={12u,8u,4u,4u,4u};
    const unsigned is34=bands==91u;
    const unsigned nlow=is34?5u:3u;
    const unsigned *splits=is34?split34:split20;
    const unsigned high_start=is34?32u:10u;
    for(unsigned slot=0;slot<PS_SYNTH_SLOTS;slot++){
        const SautiAacSbrComplex *src=hybrid+(size_t)slot*bands;
        SautiAacSbrComplex *dst=qmf+(size_t)slot*PS_SYNTH_QMF_BANDS;
        unsigned start=0u;
        for(unsigned b=0;b<nlow;b++){
            double re=0.0,im=0.0;
            for(unsigned j=0;j<splits[b];j++){
                re+=(double)src[start+j].re;
                im+=(double)src[start+j].im;
            }
            if(!isfinite(re)||!isfinite(im)||
               fabs(re)>3.402823466e38||fabs(im)>3.402823466e38)
                return 0;
            dst[b]=(SautiAacSbrComplex){(float)re,(float)im};
            start+=splits[b];
        }
        memcpy(dst+nlow,src+high_start,
               (PS_SYNTH_QMF_BANDS-nlow)*sizeof(*dst));
    }
    return 1;
}
int sauti_aac_ps_hybrid_synthesize(const SautiAacPsHybridSchedule *schedule,
                                   const SautiAacSbrComplex *hybrid,size_t hybrid_count,
                                   SautiAacSbrComplex *qmf,size_t qmf_count){
    if(!validate_schedule(schedule)||!hybrid||!qmf)return 0;
    const size_t n=(size_t)PS_SYNTH_SLOTS*schedule->hybrid_bands;
    const size_t m=(size_t)PS_SYNTH_SLOTS*PS_SYNTH_QMF_BANDS;
    if(hybrid_count<n||qmf_count<m||
       !disjoint(hybrid,n*sizeof(*hybrid),qmf,m*sizeof(*qmf))||
       !disjoint(schedule,sizeof(*schedule),qmf,m*sizeof(*qmf))||
       !validate_samples(hybrid,n))return 0;
    SautiAacSbrComplex temp[PS_SYNTH_SLOTS*PS_SYNTH_QMF_BANDS];
    if(!synth_raw(hybrid,schedule->hybrid_bands,temp))return 0;
    memcpy(qmf,temp,m*sizeof(*qmf));
    return 1;
}
int sauti_aac_ps_hybrid_synthesize_stereo(const SautiAacPsHybridSchedule *schedule,
                                          const SautiAacSbrComplex *left,size_t left_count,
                                          const SautiAacSbrComplex *right,size_t right_count,
                                          SautiAacSbrComplex *left_qmf,size_t left_qmf_count,
                                          SautiAacSbrComplex *right_qmf,size_t right_qmf_count){
    if(!validate_schedule(schedule)||!left||!right||!left_qmf||!right_qmf)
        return 0;
    const size_t n=(size_t)PS_SYNTH_SLOTS*schedule->hybrid_bands;
    const size_t m=(size_t)PS_SYNTH_SLOTS*PS_SYNTH_QMF_BANDS;
    if(left_count<n||right_count<n||left_qmf_count<m||right_qmf_count<m)
        return 0;
    const size_t nb=n*sizeof(*left),mb=m*sizeof(*left_qmf);
    if(!disjoint(left,nb,right,nb)||
       !disjoint(left,nb,left_qmf,mb)||!disjoint(left,nb,right_qmf,mb)||
       !disjoint(right,nb,left_qmf,mb)||!disjoint(right,nb,right_qmf,mb)||
       !disjoint(left_qmf,mb,right_qmf,mb)||
       !disjoint(schedule,sizeof(*schedule),left_qmf,mb)||
       !disjoint(schedule,sizeof(*schedule),right_qmf,mb)||
       !validate_samples(left,n)||!validate_samples(right,n))
        return 0;
    SautiAacSbrComplex ltmp[PS_SYNTH_SLOTS*PS_SYNTH_QMF_BANDS];
    SautiAacSbrComplex rtmp[PS_SYNTH_SLOTS*PS_SYNTH_QMF_BANDS];
    if(!synth_raw(left,schedule->hybrid_bands,ltmp)||
       !synth_raw(right,schedule->hybrid_bands,rtmp))return 0;
    memcpy(left_qmf,ltmp,mb);
    memcpy(right_qmf,rtmp,mb);
    return 1;
}
