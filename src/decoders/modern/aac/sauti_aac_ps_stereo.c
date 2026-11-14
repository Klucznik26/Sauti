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
int sauti_aac_ps_stereo_hybrid_apply(
    const SautiAacPsHybridSchedule *schedule,
    const SautiAacPsComplexMatrix *matrix,
    const SautiAacSbrComplex *qmf,size_t qmf_count,
    SautiAacPsStereoState *state,
    SautiAacSbrComplex *left,size_t left_count,
    SautiAacSbrComplex *right,size_t right_count){
    if(!schedule||!matrix||!qmf||!state||!left||!right)return 0;
    const unsigned bands=schedule->hybrid_bands,pars=schedule->parameter_bands;
    if(!((pars==20u&&bands==71u)||(pars==34u&&bands==91u))||
       matrix->bands!=pars||matrix->env_count!=schedule->env_count||
       matrix->has_phase>1u||matrix->env_count==0u||
       matrix->env_count>SAUTI_AAC_PS_MAX_ENVELOPES||
       qmf_count<38u*64u||left_count<(size_t)32u*bands||
       right_count<(size_t)32u*bands)return 0;
    const size_t data_bytes=(size_t)32u*bands*sizeof(*left);
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
    const size_t count=(size_t)32u*bands;
    if(!sauti_aac_ps_qmf_bank_analyze(schedule,qmf,qmf_count,&next.qmf_low,
                                      direct,count)||
       !sauti_aac_ps_decorrelator_apply(schedule,direct,count,
                                        &next.decorrelator,diffuse,count)||
       !sauti_aac_ps_mix_complex_apply(schedule,matrix,direct,count,
                                       diffuse,count,ltmp,count,rtmp,count))
        return 0;
    for(size_t i=0;i<count;i++)
        if(!isfinite(ltmp[i].re)||!isfinite(ltmp[i].im)||
           !isfinite(rtmp[i].re)||!isfinite(rtmp[i].im))return 0;
    memcpy(left,ltmp,data_bytes);
    memcpy(right,rtmp,data_bytes);
    *state=next;
    return 1;
}
