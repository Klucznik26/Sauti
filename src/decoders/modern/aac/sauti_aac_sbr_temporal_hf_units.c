/* SBR-54i: alternate high-band QMF amplitude unit-domain A/B.
 * Pure, transactional transform; NOT a normative HE-AAC audio path. */
#include "sauti_aac_sbr_temporal_hf_units.h"
#include <float.h>
#include <math.h>
#include <string.h>

#define SLOT_COUNT 32u
#define BAND_COUNT 64u
#define COUNT (SLOT_COUNT*BAND_COUNT)

int sauti_aac_sbr_temporal_hf_units_float(
    const SautiAacSbrComplex *high,size_t high_count,
    const SautiAacSbrTemporalNoiseFrame *noise,
    SautiAacSbrComplex *output,size_t output_count){
    if(!high || !noise || !output ||
       high_count<COUNT || output_count<COUNT)return 0;
    SautiAacSbrComplex staged[COUNT];
    for(unsigned t=0u;t<SLOT_COUNT;t++){
        unsigned started=0u,ended=0u;
        for(unsigned k=0u;k<BAND_COUNT;k++){
            const unsigned idx=t*BAND_COUNT+k;
            const unsigned active=noise->active[idx];
            const SautiAacSbrComplex x=high[idx];
            if(active>1u || !isfinite(x.re) || !isfinite(x.im) ||
               (active && ended))return 0;
            if(!active){
                if(started)ended=1u;
                if(x.re!=0.0f || x.im!=0.0f)return 0;
                staged[idx]=x;
            }else{
                started=1u;
                const double re=(double)x.re/SAUTI_AAC_SBR_HF_DOMAIN_FACTOR;
                const double im=(double)x.im/SAUTI_AAC_SBR_HF_DOMAIN_FACTOR;
                if(!isfinite(re)||!isfinite(im)||
                   fabs(re)>FLT_MAX || fabs(im)>FLT_MAX)return 0;
                staged[idx].re=(float)re;
                staged[idx].im=(float)im;
            }
        }
        if(!started)return 0;
    }
    memcpy(output,staged,sizeof(staged));
    return 1;
}
