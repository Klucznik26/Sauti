#include "sauti_aac_ps_decorrelator.h"
#include <math.h>
#include <stdint.h>
#include <string.h>

static int separate(const void *a,size_t an,const void *b,size_t bn){
    const uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x<=y?y-x>=an:x-y>=bn;
}

int sauti_aac_ps_decorrelator_apply(const SautiAacPsHybridSchedule *schedule,
                                    const SautiAacSbrComplex *direct,size_t direct_count,
                                    SautiAacPsDecorrelatorState *state,
                                    SautiAacSbrComplex *out,size_t out_count){
    if(!schedule||!direct||!state||!out)return 0;
    const unsigned bands=schedule->hybrid_bands;
    const unsigned pars=schedule->parameter_bands;
    if(!((pars==20u&&bands==71u)||(pars==34u&&bands==91u))||
       direct_count<(size_t)32u*bands||out_count<(size_t)32u*bands)return 0;
    const size_t bytes=(size_t)32u*bands*sizeof(*out);
    if(!separate(direct,bytes,out,bytes)||
       !separate(direct,bytes,state,sizeof(*state))||
       !separate(out,bytes,state,sizeof(*state)))return 0;

    /* Do not expose a partially advanced state when the second DSP
     * operation fails. The transient and all-pass submodules each
     * validate their full inputs and update only their local copies. */
    SautiAacPsDecorrelatorState next=*state;
    float gains[32u*34u];
    SautiAacSbrComplex tmp[32u*91u];
    if(!sauti_aac_ps_transient_gains(schedule,direct,direct_count,
                                    &next.transient,gains,(size_t)32u*pars)||
       !sauti_aac_ps_decorrelate(schedule,direct,direct_count,
                                &next.decorrelation,tmp,(size_t)32u*bands))
        return 0;
    for(unsigned slot=0;slot<32u;slot++){
        for(unsigned k=0;k<bands;k++){
            const float gain=gains[(size_t)slot*pars+
                                   schedule->hybrid_to_parameter[k]];
            const size_t i=(size_t)slot*bands+k;
            const double re=(double)tmp[i].re*gain;
            const double im=(double)tmp[i].im*gain;
            if(!isfinite(re)||!isfinite(im)||(fabs(re)>3.402823466e38)||
               (fabs(im)>3.402823466e38))return 0;
            tmp[i].re=(float)re;
            tmp[i].im=(float)im;
        }
    }
    memcpy(out,tmp,bytes);
    *state=next;
    return 1;
}
