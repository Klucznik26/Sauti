#include "sauti_aac_ps_stereo.h"
#include <math.h>
#include <stdint.h>
#include <string.h>

static int disjoint(const void *a,size_t n,const void *b,size_t m){
    const uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x<=y?y-x>=n:x-y>=m;
}
static int finite_coeff(SautiAacPsComplexCoefficient c){
    return isfinite(c.re)&&isfinite(c.im);
}
/* Previous-envelope mixing values when the band layout changes (20 <-> 34 parameter bands): the same band mapping the parameters use
 * (table 8.46 and its inverse), applied to every real and imaginary component of the four coefficients. */
static void map_value_34_to_20(const float s[34],float d[34]){
    d[0]=(2.0f*s[0]+s[1])*0.33333333f;d[1]=(s[1]+2.0f*s[2])*0.33333333f;
    d[2]=(2.0f*s[3]+s[4])*0.33333333f;d[3]=(s[4]+2.0f*s[5])*0.33333333f;
    d[4]=(s[6]+s[7])*0.5f;d[5]=(s[8]+s[9])*0.5f;d[6]=s[10];d[7]=s[11];
    d[8]=(s[12]+s[13])*0.5f;d[9]=(s[14]+s[15])*0.5f;d[10]=s[16];d[11]=s[17];d[12]=s[18];d[13]=s[19];
    d[14]=(s[20]+s[21])*0.5f;d[15]=(s[22]+s[23])*0.5f;d[16]=(s[24]+s[25])*0.5f;d[17]=(s[26]+s[27])*0.5f;
    d[18]=(s[28]+s[29]+s[30]+s[31])*0.25f;d[19]=(s[32]+s[33])*0.5f;
    for(unsigned i=20u;i<34u;i++)d[i]=0.0f;
}
static void map_value_20_to_34(const float s[34],float d[34]){
    d[0]=s[0];d[1]=(s[0]+s[1])*0.5f;d[2]=s[1];d[3]=s[2];d[4]=(s[2]+s[3])*0.5f;d[5]=s[3];d[6]=s[4];d[7]=s[4];
    d[8]=s[5];d[9]=s[5];d[10]=s[6];d[11]=s[7];d[12]=s[8];d[13]=s[8];d[14]=s[9];d[15]=s[9];d[16]=s[10];d[17]=s[11];
    d[18]=s[12];d[19]=s[13];d[20]=s[14];d[21]=s[14];d[22]=s[15];d[23]=s[15];d[24]=s[16];d[25]=s[16];d[26]=s[17];d[27]=s[17];
    d[28]=s[18];d[29]=s[18];d[30]=s[18];d[31]=s[18];d[32]=s[19];d[33]=s[19];
}
static void map_previous_layout(SautiAacPsComplexMatrixCoefficients prev[SAUTI_AAC_PS_MAX_PARAMETERS],unsigned from,unsigned to){
    for(unsigned comp=0u;comp<8u;comp++){
        float src[34],dst[34];
        for(unsigned b=0u;b<34u;b++){
            SautiAacPsComplexCoefficient *c[4]={&prev[b].h11,&prev[b].h12,&prev[b].h21,&prev[b].h22};
            src[b]=(comp&1u)?c[comp>>1u]->im:c[comp>>1u]->re;
        }
        if(from==34u&&to==20u)map_value_34_to_20(src,dst);else map_value_20_to_34(src,dst);
        for(unsigned b=0u;b<34u;b++){
            SautiAacPsComplexCoefficient *c[4]={&prev[b].h11,&prev[b].h12,&prev[b].h21,&prev[b].h22};
            if(comp&1u)c[comp>>1u]->im=dst[b];else c[comp>>1u]->re=dst[b];
        }
    }
}
int sauti_aac_ps_stereo_hybrid_apply(
    const SautiAacPsHybridSchedule *schedule,
    const SautiAacPsComplexMatrix *matrix,
    const SautiAacSbrComplex *qmf,size_t qmf_count,
    SautiAacPsStereoState *state,
    SautiAacSbrComplex *left,size_t left_count,
    SautiAacSbrComplex *right,size_t right_count){
    if(!schedule||!matrix||!qmf||!state||!left||!right)return 0;
    const unsigned bands=schedule->hybrid_bands,pars=schedule->parameter_bands;
    const unsigned S=sauti_aac_ps_schedule_slots(schedule);
    if(!((pars==20u&&bands==71u)||(pars==34u&&bands==91u))||
       matrix->bands!=pars||matrix->env_count!=schedule->env_count||
       matrix->has_phase>1u||matrix->env_count==0u||
       matrix->env_count>SAUTI_AAC_PS_MAX_ENVELOPES||
       qmf_count<38u*64u||left_count<(size_t)S*bands||
       right_count<(size_t)S*bands)return 0;
    const size_t data_bytes=(size_t)S*bands*sizeof(*left);
    const size_t qmf_bytes=(size_t)38u*64u*sizeof(*qmf);
    if(!disjoint(left,data_bytes,right,data_bytes)||
       !disjoint(left,data_bytes,qmf,qmf_bytes)||
       !disjoint(right,data_bytes,qmf,qmf_bytes)||
       !disjoint(state,sizeof(*state),qmf,qmf_bytes)||
       !disjoint(state,sizeof(*state),left,data_bytes)||
       !disjoint(state,sizeof(*state),right,data_bytes)||
       !disjoint(schedule,sizeof(*schedule),left,data_bytes)||
       !disjoint(schedule,sizeof(*schedule),right,data_bytes)||
       !disjoint(matrix,sizeof(*matrix),left,data_bytes)||
       !disjoint(matrix,sizeof(*matrix),right,data_bytes))return 0;
    for(unsigned e=0;e<matrix->env_count;e++)
        for(unsigned b=0;b<pars;b++){
            SautiAacPsComplexMatrixCoefficients x=matrix->coefficient[e][b];
            if(!finite_coeff(x.h11)||!finite_coeff(x.h12)||
               !finite_coeff(x.h21)||!finite_coeff(x.h22))return 0;
        }
    /* Stage both DSP histories. The independent components publish only
     * to local copies; their partial success is never externally visible. */
    SautiAacPsStereoState next=*state;
    SautiAacSbrComplex direct[32u*91u],diffuse[32u*91u];
    SautiAacSbrComplex ltmp[32u*91u],rtmp[32u*91u];
    const size_t count=(size_t)S*bands;
    /* start of the stream: zero (the matrix ramps up); topology change 20 <-> 34: the previous end values are mapped to the new layout */
    SautiAacPsComplexMatrixCoefficients start[SAUTI_AAC_PS_MAX_PARAMETERS];
    memcpy(start,next.previous,sizeof(start));
    if(next.previous_bands!=0u&&next.previous_bands!=pars)map_previous_layout(start,next.previous_bands,pars);
    if(!sauti_aac_ps_qmf_bank_analyze(schedule,qmf,qmf_count,&next.qmf_low,
                                      direct,count)||
       !sauti_aac_ps_decorrelator_apply(schedule,direct,count,
                                        &next.decorrelator,diffuse,count)||
       !sauti_aac_ps_mix_interpolated_apply(schedule,matrix,start,direct,count,diffuse,count,ltmp,count,rtmp,count))
        return 0;
    for(unsigned b=0;b<pars;b++)next.previous[b]=matrix->coefficient[matrix->env_count-1u][b];
    next.previous_bands=pars;
    for(size_t i=0;i<count;i++)
        if(!isfinite(ltmp[i].re)||!isfinite(ltmp[i].im)||
           !isfinite(rtmp[i].re)||!isfinite(rtmp[i].im))return 0;
    memcpy(left,ltmp,data_bytes);
    memcpy(right,rtmp,data_bytes);
    *state=next;
    return 1;
}
