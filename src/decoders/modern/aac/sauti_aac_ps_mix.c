#include "sauti_aac_ps_mix.h"

#include <math.h>
#include <string.h>

extern void sauti_aac_ps_mix_strided_asm(
    const SautiAacSbrComplex *direct,const SautiAacSbrComplex *decorrelated,
    SautiAacSbrComplex *left,SautiAacSbrComplex *right,
    const SautiAacPsMatrixCoefficients *coefficient,
    size_t samples,size_t stride_bytes);
extern void sauti_aac_ps_mix_complex_strided_asm(
    const SautiAacSbrComplex *direct,const SautiAacSbrComplex *decorrelated,
    SautiAacSbrComplex *left,SautiAacSbrComplex *right,
    const SautiAacPsComplexMatrixCoefficients *coefficient,
    size_t samples,size_t stride_bytes);

static int finite_complex(SautiAacSbrComplex x){
    return isfinite(x.re)&&isfinite(x.im);
}

int sauti_aac_ps_mix_real_apply(const SautiAacPsHybridSchedule *schedule,
                                const SautiAacPsMatrix *matrix,
                                const SautiAacSbrComplex *direct,size_t direct_count,
                                const SautiAacSbrComplex *decorrelated,size_t decorrelated_count,
                                SautiAacSbrComplex *left,size_t left_count,
                                SautiAacSbrComplex *right,size_t right_count){
    if(!schedule||!matrix||!direct||!decorrelated||!left||!right||left==right||
       (schedule->parameter_bands!=20u&&schedule->parameter_bands!=34u)||
       (schedule->hybrid_bands!=71u&&schedule->hybrid_bands!=91u)||
       matrix->bands!=schedule->parameter_bands||!matrix->env_count||
       matrix->env_count!=schedule->env_count)return 0;
    const size_t need=(size_t)SAUTI_AAC_PS_QMF_SLOTS*schedule->hybrid_bands;
    if(direct_count<need||decorrelated_count<need||left_count<need||right_count<need)return 0;
    for(size_t i=0u;i<need;i++)if(!finite_complex(direct[i])||!finite_complex(decorrelated[i]))return 0;
    for(unsigned t=0u;t<SAUTI_AAC_PS_QMF_SLOTS;t++){
        if(schedule->slot_to_env[t]>=matrix->env_count)return 0;
    }
    for(unsigned k=0u;k<schedule->hybrid_bands;k++){
        if(schedule->hybrid_to_parameter[k]>=matrix->bands)return 0;
    }
    SautiAacSbrComplex left_stage[SAUTI_AAC_PS_QMF_SLOTS*SAUTI_AAC_PS_MAX_HYBRID_BANDS];
    SautiAacSbrComplex right_stage[SAUTI_AAC_PS_QMF_SLOTS*SAUTI_AAC_PS_MAX_HYBRID_BANDS];
    const size_t stride=(size_t)schedule->hybrid_bands*sizeof(*direct);
    for(unsigned k=0u;k<schedule->hybrid_bands;k++){
        unsigned first=0u;
        while(first<SAUTI_AAC_PS_QMF_SLOTS){
            const unsigned env=schedule->slot_to_env[first];
            unsigned end=first+1u;
            while(end<SAUTI_AAC_PS_QMF_SLOTS&&schedule->slot_to_env[end]==env)end++;
            const unsigned band=schedule->hybrid_to_parameter[k];
            sauti_aac_ps_mix_strided_asm(direct+(size_t)first*schedule->hybrid_bands+k,
                decorrelated+(size_t)first*schedule->hybrid_bands+k,
                left_stage+(size_t)first*schedule->hybrid_bands+k,
                right_stage+(size_t)first*schedule->hybrid_bands+k,
                &matrix->coefficient[env][band],end-first,stride);
            first=end;
        }
    }
    memcpy(left,left_stage,need*sizeof(*left));
    memcpy(right,right_stage,need*sizeof(*right));
    return 1;
}

int sauti_aac_ps_mix_complex_apply(const SautiAacPsHybridSchedule *schedule,
                                   const SautiAacPsComplexMatrix *matrix,
                                   const SautiAacSbrComplex *direct,size_t direct_count,
                                   const SautiAacSbrComplex *decorrelated,size_t decorrelated_count,
                                   SautiAacSbrComplex *left,size_t left_count,
                                   SautiAacSbrComplex *right,size_t right_count){
    if(!schedule||!matrix||!direct||!decorrelated||!left||!right||left==right||
       (schedule->parameter_bands!=20u&&schedule->parameter_bands!=34u)||
       (schedule->hybrid_bands!=71u&&schedule->hybrid_bands!=91u)||
       matrix->bands!=schedule->parameter_bands||!matrix->env_count||
       matrix->env_count!=schedule->env_count||matrix->has_phase>1u)return 0;
    const size_t need=(size_t)SAUTI_AAC_PS_QMF_SLOTS*schedule->hybrid_bands;
    if(direct_count<need||decorrelated_count<need||left_count<need||right_count<need)return 0;
    for(size_t i=0u;i<need;i++)if(!finite_complex(direct[i])||!finite_complex(decorrelated[i]))return 0;
    for(unsigned t=0u;t<SAUTI_AAC_PS_QMF_SLOTS;t++)if(schedule->slot_to_env[t]>=matrix->env_count)return 0;
    for(unsigned k=0u;k<schedule->hybrid_bands;k++){
        if(schedule->hybrid_to_parameter[k]>=matrix->bands||
           (schedule->phase_imaginary_sign[k]!=1&&schedule->phase_imaginary_sign[k]!=-1))return 0;
    }
    SautiAacSbrComplex left_stage[SAUTI_AAC_PS_QMF_SLOTS*SAUTI_AAC_PS_MAX_HYBRID_BANDS];
    SautiAacSbrComplex right_stage[SAUTI_AAC_PS_QMF_SLOTS*SAUTI_AAC_PS_MAX_HYBRID_BANDS];
    const size_t stride=(size_t)schedule->hybrid_bands*sizeof(*direct);
    for(unsigned k=0u;k<schedule->hybrid_bands;k++){
        unsigned first=0u;
        while(first<SAUTI_AAC_PS_QMF_SLOTS){
            const unsigned env=schedule->slot_to_env[first];
            unsigned end=first+1u;
            while(end<SAUTI_AAC_PS_QMF_SLOTS&&schedule->slot_to_env[end]==env)end++;
            SautiAacPsComplexMatrixCoefficients c=
                matrix->coefficient[env][schedule->hybrid_to_parameter[k]];
            if(schedule->phase_imaginary_sign[k]<0){
                c.h11.im=-c.h11.im;c.h12.im=-c.h12.im;
                c.h21.im=-c.h21.im;c.h22.im=-c.h22.im;
            }
            sauti_aac_ps_mix_complex_strided_asm(direct+(size_t)first*schedule->hybrid_bands+k,
                decorrelated+(size_t)first*schedule->hybrid_bands+k,
                left_stage+(size_t)first*schedule->hybrid_bands+k,
                right_stage+(size_t)first*schedule->hybrid_bands+k,
                &c,end-first,stride);
            first=end;
        }
    }
    memcpy(left,left_stage,need*sizeof(*left));
    memcpy(right,right_stage,need*sizeof(*right));
    return 1;
}

static SautiAacPsComplexMatrixCoefficients lerp_coeff(const SautiAacPsComplexMatrixCoefficients *a,
                                                      const SautiAacPsComplexMatrixCoefficients *b,double t){
    SautiAacPsComplexMatrixCoefficients r;
    const SautiAacPsComplexCoefficient *pa[4]={&a->h11,&a->h12,&a->h21,&a->h22};
    const SautiAacPsComplexCoefficient *pb[4]={&b->h11,&b->h12,&b->h21,&b->h22};
    SautiAacPsComplexCoefficient *pr[4]={&r.h11,&r.h12,&r.h21,&r.h22};
    for(unsigned i=0;i<4;i++){
        pr[i]->re=(float)((double)pa[i]->re+t*((double)pb[i]->re-(double)pa[i]->re));
        pr[i]->im=(float)((double)pa[i]->im+t*((double)pb[i]->im-(double)pa[i]->im));
    }
    return r;
}

int sauti_aac_ps_mix_interpolated_apply(const SautiAacPsHybridSchedule *schedule,
                                        const SautiAacPsComplexMatrix *matrix,
                                        const SautiAacPsComplexMatrixCoefficients *previous,
                                        const SautiAacSbrComplex *direct,size_t direct_count,
                                        const SautiAacSbrComplex *decorrelated,size_t decorrelated_count,
                                        SautiAacSbrComplex *left,size_t left_count,
                                        SautiAacSbrComplex *right,size_t right_count){
    if(!schedule||!matrix||!previous||!direct||!decorrelated||!left||!right||left==right||
       (schedule->parameter_bands!=20u&&schedule->parameter_bands!=34u)||
       (schedule->hybrid_bands!=71u&&schedule->hybrid_bands!=91u)||
       matrix->bands!=schedule->parameter_bands||!matrix->env_count||
       matrix->env_count!=schedule->env_count||matrix->has_phase>1u)return 0;
    const size_t need=(size_t)SAUTI_AAC_PS_QMF_SLOTS*schedule->hybrid_bands;
    if(direct_count<need||decorrelated_count<need||left_count<need||right_count<need)return 0;
    for(size_t i=0u;i<need;i++)if(!finite_complex(direct[i])||!finite_complex(decorrelated[i]))return 0;
    for(unsigned t=0u;t<SAUTI_AAC_PS_QMF_SLOTS;t++)if(schedule->slot_to_env[t]>=matrix->env_count)return 0;
    for(unsigned k=0u;k<schedule->hybrid_bands;k++){
        if(schedule->hybrid_to_parameter[k]>=matrix->bands||
           (schedule->phase_imaginary_sign[k]!=1&&schedule->phase_imaginary_sign[k]!=-1))return 0;
    }
    SautiAacSbrComplex left_stage[SAUTI_AAC_PS_QMF_SLOTS*SAUTI_AAC_PS_MAX_HYBRID_BANDS];
    SautiAacSbrComplex right_stage[SAUTI_AAC_PS_QMF_SLOTS*SAUTI_AAC_PS_MAX_HYBRID_BANDS];
    const unsigned nb=schedule->hybrid_bands;
    for(unsigned k=0u;k<nb;k++){
        const unsigned par=schedule->hybrid_to_parameter[k];
        const int flip=schedule->phase_imaginary_sign[k]<0;
        unsigned first=0u;
        while(first<SAUTI_AAC_PS_QMF_SLOTS){
            const unsigned env=schedule->slot_to_env[first];
            unsigned end=first+1u;
            while(end<SAUTI_AAC_PS_QMF_SLOTS&&schedule->slot_to_env[end]==env)end++;
            const unsigned len=end-first;
            SautiAacPsComplexMatrixCoefficients from=env?matrix->coefficient[env-1u][par]:previous[par];
            SautiAacPsComplexMatrixCoefficients to=matrix->coefficient[env][par];
            if(flip){
                SautiAacPsComplexCoefficient *f[4]={&from.h11,&from.h12,&from.h21,&from.h22};
                SautiAacPsComplexCoefficient *g[4]={&to.h11,&to.h12,&to.h21,&to.h22};
                for(unsigned i=0;i<4;i++){f[i]->im=-f[i]->im;g[i]->im=-g[i]->im;}
            }
            for(unsigned n=first;n<end;n++){
                const SautiAacPsComplexMatrixCoefficients c=lerp_coeff(&from,&to,(double)(n-first+1u)/(double)len);
                const SautiAacSbrComplex d=direct[(size_t)n*nb+k],x=decorrelated[(size_t)n*nb+k];
                /* L = h11*direct + h21*decorrelated,  R = h12*direct + h22*decorrelated (complex products) */
                const double lre=(double)c.h11.re*d.re-(double)c.h11.im*d.im+(double)c.h21.re*x.re-(double)c.h21.im*x.im;
                const double lim=(double)c.h11.re*d.im+(double)c.h11.im*d.re+(double)c.h21.re*x.im+(double)c.h21.im*x.re;
                const double rre=(double)c.h12.re*d.re-(double)c.h12.im*d.im+(double)c.h22.re*x.re-(double)c.h22.im*x.im;
                const double rim=(double)c.h12.re*d.im+(double)c.h12.im*d.re+(double)c.h22.re*x.im+(double)c.h22.im*x.re;
                left_stage[(size_t)n*nb+k].re=(float)lre;left_stage[(size_t)n*nb+k].im=(float)lim;
                right_stage[(size_t)n*nb+k].re=(float)rre;right_stage[(size_t)n*nb+k].im=(float)rim;
            }
            first=end;
        }
    }
    for(size_t i=0u;i<need;i++)
        if(!finite_complex(left_stage[i])||!finite_complex(right_stage[i]))return 0;
    memcpy(left,left_stage,need*sizeof(*left));
    memcpy(right,right_stage,need*sizeof(*right));
    return 1;
}
