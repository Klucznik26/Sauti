#include "sauti_aac_ps_transient.h"
#include <math.h>
#include <stdint.h>
#include <string.h>

static int separate(const void *a,size_t an,const void *b,size_t bn){
    const uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x<=y?y-x>=an:x-y>=bn;
}
int sauti_aac_ps_transient_gains(const SautiAacPsHybridSchedule *schedule,
                                 const SautiAacSbrComplex *input,size_t input_count,
                                 SautiAacPsTransientState *state,
                                 float *gains,size_t gain_count){
    if(!schedule||!input||!state||!gains)return 0;
    const unsigned bands=schedule->parameter_bands;
    const unsigned hybrid=schedule->hybrid_bands;
    if(!((bands==20u&&hybrid==71u)||(bands==34u&&hybrid==91u))||
       input_count<(size_t)32u*hybrid||gain_count<(size_t)32u*bands||
       (state->bands!=0u&&state->bands!=20u&&state->bands!=34u)||
       !schedule->env_count||schedule->env_count>SAUTI_AAC_PS_MAX_ENVELOPES)
        return 0;
    if(!separate(input,(size_t)32u*hybrid*sizeof(*input),gains,
                 (size_t)32u*bands*sizeof(*gains))||
       !separate(input,(size_t)32u*hybrid*sizeof(*input),state,sizeof(*state))||
       !separate(gains,(size_t)32u*bands*sizeof(*gains),state,sizeof(*state)))
        return 0;
    for(unsigned k=0;k<hybrid;k++)
        if(schedule->hybrid_to_parameter[k]>=bands)return 0;
    for(unsigned n=0;n<32u;n++)
        if(schedule->slot_to_env[n]>=schedule->env_count)return 0;

    SautiAacPsTransientState next=*state;
    if(next.bands!=bands)memset(&next,0,sizeof(next));
    next.bands=bands;
    for(unsigned b=0;b<bands;b++)
        if(!isfinite(next.peak[b])||!isfinite(next.smooth[b])||
           !isfinite(next.diff[b])||
           next.peak[b]<0.0f||next.smooth[b]<0.0f||next.diff[b]<0.0f)
            return 0;

    float temp[32u*34u];
    /* Reference: ffmpeg n6.1 aacps.c, transient detection. First sum energy
     * of every hybrid subband mapped to each parameter band in each slot. */
    for(unsigned n=0;n<32u;n++){
        double energy[34]={0};
        for(unsigned k=0;k<hybrid;k++){
            const SautiAacSbrComplex v=input[(size_t)n*hybrid+k];
            if(!isfinite(v.re)||!isfinite(v.im))return 0;
            const double e=(double)v.re*v.re+(double)v.im*v.im;
            const unsigned b=schedule->hybrid_to_parameter[k];
            energy[b]+=e;
            if(!isfinite(energy[b])||energy[b]>1.0e30)return 0;
        }
        for(unsigned b=0;b<bands;b++){
            const double peak=fmax(0.76592833836465*(double)next.peak[b],energy[b]);
            const double smooth=(double)next.smooth[b]+0.25*(energy[b]-next.smooth[b]);
            const double diff=(double)next.diff[b]+0.25*(peak-energy[b]-next.diff[b]);
            if(!isfinite(peak)||!isfinite(smooth)||!isfinite(diff)||
               peak>1.0e30||smooth>1.0e30||diff>1.0e30)return 0;
            next.peak[b]=(float)peak;
            next.smooth[b]=(float)fmax(0.0,smooth);
            next.diff[b]=(float)fmax(0.0,diff);
            const double den=1.5*diff;
            const double gain=den>smooth?smooth/den:1.0;
            if(!isfinite(gain)||gain<0.0||gain>1.0)return 0;
            temp[(size_t)n*bands+b]=(float)gain;
        }
    }
    memcpy(gains,temp,(size_t)32u*bands*sizeof(*gains));
    *state=next;
    return 1;
}
