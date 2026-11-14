#include "sauti_aac_ps_qmf_low.h"
#include <math.h>
#include <stdint.h>
#include <string.h>

/* 13-tap PS hybrid prototype half-windows, ordered from the edge to
 * the centre. Numerical reference: ISO/IEC 14496-3 PS filter prototypes;
 * compare independently against FFmpeg aacps_tablegen.h before release.
 * Source-provenance / license review is a separate release gate. */
static const double p8_0[7]={0.00746082949812,0.02270420949825,0.04546865930473,
 0.07266113929591,0.09885108575264,0.11793710567217,0.125};
static const double p12_0[7]={0.04081179924692,0.03812810994926,0.05144908135699,
 0.06399831151592,0.07428313801106,0.08100347892914,0.08333333333333};
static const double p8_1[7]={0.01565675600122,0.03752716391991,0.05417891378782,
 0.08417044116767,0.10307344158036,0.12222452249753,0.125};
static const double p4_2[7]={-0.05908211155639,-0.04871498374946,0.0,
 0.07778723915851,0.16486303567403,0.23279856662996,0.25};
static const double p2_1[7]={0.0,0.01899487526049,0.0,-0.07293139167538,
 0.0,0.30596630545168,0.5};
static const double pi=3.14159265358979323846264338327950288;

/* Chronological 13-sample window around a QMF slot. */
static SautiAacSbrComplex sample_at(const SautiAacSbrComplex *qmf,
                                     const SautiAacPsQmfLowState *state,
                                     unsigned band,int position){
    if(position<0)return state->history[band][position+6];
    return qmf[(size_t)position*64u+band];
}

/* Direct 13-tap modulated analysis; no FFmpeg dependency at runtime. */
static void split_complex(const SautiAacSbrComplex *qmf,
                          const SautiAacPsQmfLowState *state,unsigned band,
                          unsigned slot,unsigned bins,const double proto[7],
                          SautiAacSbrComplex *out){
    for(unsigned k=0;k<bins;k++){
        double re=0.0,im=0.0;
        for(unsigned tap=0;tap<13u;tap++){
            const int off=(int)tap-6;
            const double p=proto[tap<=6u?tap:12u-tap];
            const double theta=(2.0*pi*(k+0.5)*(double)off)/(double)bins;
            const double cr=p*cos(theta), ci=-p*sin(theta);
            const SautiAacSbrComplex x=sample_at(qmf,state,band,(int)slot+off);
            re+=cr*x.re-ci*x.im;
            im+=cr*x.im+ci*x.re;
        }
        out[k].re=(float)re;out[k].im=(float)im;
    }
}
static void split_two(const SautiAacSbrComplex *qmf,
                      const SautiAacPsQmfLowState *state,unsigned band,
                      unsigned slot,int reverse,SautiAacSbrComplex *out){
    const SautiAacSbrComplex x=sample_at(qmf,state,band,(int)slot);
    double ir=p2_1[6]*x.re, ii=p2_1[6]*x.im,or_=0.0,oi=0.0;
    for(unsigned j=1;j<=5;j+=2){
        const SautiAacSbrComplex a=sample_at(qmf,state,band,(int)slot+(int)j-6);
        const SautiAacSbrComplex b=sample_at(qmf,state,band,(int)slot+6-(int)j);
        or_+=p2_1[j]*(a.re+b.re);oi+=p2_1[j]*(a.im+b.im);
    }
    out[reverse]=(SautiAacSbrComplex){(float)(ir+or_),(float)(ii+oi)};
    out[!reverse]=(SautiAacSbrComplex){(float)(ir-or_),(float)(ii-oi)};
}
static int nonoverlap(const void *a,size_t asz,const void *b,size_t bsz){
    const uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x<=y ? y-x>=asz : x-y>=bsz;
}
int sauti_aac_ps_qmf_low_analyze(const SautiAacPsHybridSchedule *schedule,
                                 const SautiAacSbrComplex *qmf,size_t qmf_count,
                                 SautiAacPsQmfLowState *state,
                                 SautiAacSbrComplex *low,size_t low_count){
    if(!schedule||!qmf||!state||!low||
       qmf_count<SAUTI_AAC_PS_LOW_QMF_INPUT||
       state->initialized>1u)return 0;
    unsigned bands,source_bands;
    if(schedule->parameter_bands==20u&&schedule->hybrid_bands==71u){
        bands=10u;source_bands=3u;
    }else if(schedule->parameter_bands==34u&&schedule->hybrid_bands==91u){
        bands=32u;source_bands=5u;
    }else return 0;
    const size_t output_samples=(size_t)32u*bands;
    if(low_count<output_samples||schedule->env_count==0u||
       schedule->env_count>SAUTI_AAC_PS_MAX_ENVELOPES||
       !nonoverlap(qmf,SAUTI_AAC_PS_LOW_QMF_INPUT*sizeof(*qmf),
                   low,output_samples*sizeof(*low))||
       !nonoverlap(state,sizeof(*state),low,output_samples*sizeof(*low))||
       !nonoverlap(state,sizeof(*state),qmf,SAUTI_AAC_PS_LOW_QMF_INPUT*sizeof(*qmf)))
        return 0;
    for(unsigned i=0;i<32u;i++)
        if(schedule->slot_to_env[i]>=schedule->env_count)return 0;
    for(unsigned i=0;i<bands;i++)
        if(schedule->hybrid_to_parameter[i]>=schedule->parameter_bands)return 0;
    /* Validate every sample that participates in the FIR and future history. */
    for(unsigned t=0;t<38u;t++)for(unsigned b=0;b<source_bands;b++){
        const SautiAacSbrComplex x=qmf[t*64u+b];
        if(!isfinite(x.re)||!isfinite(x.im))return 0;
    }
    if(state->initialized)
        for(unsigned b=0;b<source_bands;b++)for(unsigned h=0;h<6u;h++){
            const SautiAacSbrComplex x=state->history[b][h];
            if(!isfinite(x.re)||!isfinite(x.im))return 0;
        }
    const SautiAacPsQmfLowState empty={0};
    const SautiAacPsQmfLowState *prev=state->initialized?state:&empty;
    SautiAacPsQmfLowState next=*prev;
    SautiAacSbrComplex tmp[32u*SAUTI_AAC_PS_LOW_QMF_MAX_BANDS];
    for(unsigned t=0;t<32u;t++){
        SautiAacSbrComplex *dst=&tmp[t*bands];
        if(bands==10u){
            SautiAacSbrComplex eight[8];
            split_complex(qmf,prev,0u,t,8u,p8_0,eight);
            dst[0]=eight[6];dst[1]=eight[7];
            dst[2]=eight[0];dst[3]=eight[1];
            for(unsigned c=0;c<2u;c++){
                const unsigned a=2u+c,b=5u-c;
                dst[4u+c].re=eight[a].re+eight[b].re;
                dst[4u+c].im=eight[a].im+eight[b].im;
            }
            split_two(qmf,prev,1u,t,1,dst+6u);
            split_two(qmf,prev,2u,t,0,dst+8u);
        }else{
            split_complex(qmf,prev,0u,t,12u,p12_0,dst);
            split_complex(qmf,prev,1u,t,8u,p8_1,dst+12u);
            for(unsigned b=2u;b<5u;b++)
                split_complex(qmf,prev,b,t,4u,p4_2,dst+20u+(b-2u)*4u);
        }
        for(unsigned k=0;k<bands;k++)
            if(!isfinite(dst[k].re)||!isfinite(dst[k].im))return 0;
    }
    /* Retain the six samples preceding NEXT frame, not the lookahead tail. */
    for(unsigned b=0;b<source_bands;b++)for(unsigned h=0;h<6u;h++)
        next.history[b][h]=qmf[(26u+h)*64u+b];
    next.initialized=1u;
    memcpy(low,tmp,output_samples*sizeof(*low));
    *state=next;
    return 1;
}
