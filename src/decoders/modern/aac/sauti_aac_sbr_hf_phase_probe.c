/* SBR-54q: conjugate ONLY active high QMF, before independent
 * 64-QMF synthesis. This is an A/B hypothesis test, not a fix. */
#include "sauti_aac_sbr_hf_phase_probe.h"
#include <math.h>
#include <string.h>
#define COUNT (32u*64u)
int sauti_aac_sbr_hf_conjugate_probe(
    const SautiAacSbrComplex *high,size_t high_count,
    const SautiAacSbrTemporalNoiseFrame *mask,
    SautiAacSbrComplex *out,size_t out_count){
    if(!high || !mask || !out ||
       high_count<COUNT || out_count<COUNT)return 0;
    SautiAacSbrComplex stage[COUNT];
    for(unsigned slot=0u;slot<32u;slot++){
        unsigned begin=0u,end=0u;
        for(unsigned k=0u;k<64u;k++){
            const unsigned i=slot*64u+k;
            const unsigned active=mask->active[i];
            const SautiAacSbrComplex x=high[i];
            if(active>1u || !isfinite(x.re) || !isfinite(x.im) ||
               (active && end))return 0;
            if(active){
                begin=1u;
                stage[i].re=x.re;
                stage[i].im=-x.im;
            }else{
                if(begin)end=1u;
                if(x.re!=0.0f || x.im!=0.0f)return 0;
                stage[i]=x;
            }
        }
        if(!begin)return 0;
    }
    memcpy(out,stage,sizeof(stage));
    return 1;
}
