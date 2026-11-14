#include "sauti_aac_ps_qmf_high.h"
#include <math.h>
#include <string.h>

int sauti_aac_ps_qmf_high_extract(const SautiAacPsHybridSchedule *schedule,
                                  const SautiAacSbrComplex *qmf,
                                  size_t qmf_count,
                                  SautiAacSbrComplex *high,
                                  size_t high_count){
    if(!schedule||!qmf||!high||
       qmf_count < SAUTI_AAC_PS_HIGH_QMF_INPUT_SLOTS*SAUTI_AAC_PS_HIGH_QMF_BANDS)
        return 0;
    unsigned first_qmf,first_hybrid,high_bands;
    if(schedule->parameter_bands==20u && schedule->hybrid_bands==71u){
        first_qmf=3u;first_hybrid=10u;high_bands=61u;
    }else if(schedule->parameter_bands==34u && schedule->hybrid_bands==91u){
        first_qmf=5u;first_hybrid=32u;high_bands=59u;
    }else return 0;
    if(high_count < SAUTI_AAC_PS_QMF_SLOTS*high_bands ||
       schedule->env_count==0u || schedule->env_count>SAUTI_AAC_PS_MAX_ENVELOPES)
        return 0;
    for(unsigned slot=0;slot<SAUTI_AAC_PS_QMF_SLOTS;slot++){
        if(schedule->slot_to_env[slot]>=schedule->env_count)return 0;
    }
    for(unsigned k=0;k<high_bands;k++){
        if(schedule->hybrid_to_parameter[first_hybrid+k]>=schedule->parameter_bands)
            return 0;
    }
    SautiAacSbrComplex next[SAUTI_AAC_PS_QMF_SLOTS*SAUTI_AAC_PS_HIGH_QMF_MAX_BANDS];
    for(unsigned slot=0;slot<SAUTI_AAC_PS_QMF_SLOTS;slot++){
        for(unsigned k=0;k<high_bands;k++){
            const SautiAacSbrComplex x=qmf[slot*64u+first_qmf+k];
            if(!isfinite(x.re)||!isfinite(x.im))return 0;
            next[slot*high_bands+k]=x;
        }
    }
    memmove(high,next,(size_t)SAUTI_AAC_PS_QMF_SLOTS*high_bands*sizeof(*high));
    return 1;
}
