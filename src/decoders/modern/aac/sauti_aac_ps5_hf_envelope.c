#include "sauti_aac_ps5_hf_envelope.h"
#include <math.h>
#include <stdlib.h>

/* No output or input state is touched until every group validates.
 * Group size is bounded to 32 and number of envelope indices to 5. */
int sauti_aac_ps5_hf_envelope_analyze(
    const SautiAacSbrTemporalHfTargets *targets,
    const SautiAacSbrComplex *raw,size_t raw_count,
    SautiAacPs5HfEnvelopeReport *report){
    if(!targets||!raw||!report||raw_count<32u*64u||
       targets->previous_slots>6u)return 0;
    SautiAacPs5HfEnvelopeReport *next=calloc(1u,sizeof(*next));
    if(!next)return 0;
    double sum[2u][SAUTI_AAC_SBR_MAX_ENV][64u]={{{0}}};
    unsigned count[2u][SAUTI_AAC_SBR_MAX_ENV]={0};
    int ok=1;
    for(unsigned t=0;t<32u&&ok;t++){
        const SautiAacSbrTemporalHfSlot *slot=&targets->slot[t];
        const unsigned src=slot->source,env=slot->envelope;
        if(src>1u||env>=SAUTI_AAC_SBR_MAX_ENV||
           (t<targets->previous_slots?src!=0u:src!=1u)||
           slot->kx<1u||slot->kx>32u||slot->k2<=slot->kx||
           slot->k2>64u||slot->smooth_bypass>1u){ok=0;break;}
        count[src][env]++;
        for(unsigned k=slot->kx;k<slot->k2;k++){
            const SautiAacSbrComplex x=raw[(size_t)t*64u+k];
            const double e=slot->energy[k],q=slot->noise_factor[k];
            const double p=(double)x.re*x.re+(double)x.im*x.im;
            if(!isfinite(x.re)||!isfinite(x.im)||!isfinite(p)||
               !isfinite(e)||e<0.0||!isfinite(q)||q<0.0||
               slot->harmonic[k]>1u){ok=0;break;}
            sum[src][env][k]+=p;
            if(!isfinite(sum[src][env][k])){ok=0;break;}
        }
    }
    for(unsigned a=0;a<2u;a++)
        for(unsigned b=0;b<SAUTI_AAC_SBR_MAX_ENV;b++)
            if(count[a][b])next->groups++;
    for(unsigned t=0;t<32u&&ok;t++){
        const SautiAacSbrTemporalHfSlot *slot=&targets->slot[t];
        const unsigned src=slot->source,env=slot->envelope;
        for(unsigned k=slot->kx;k<slot->k2;k++){
            const SautiAacSbrComplex x=raw[(size_t)t*64u+k];
            const double p=(double)x.re*x.re+(double)x.im*x.im;
            const double e=slot->energy[k],q=slot->noise_factor[k];
            const double signal=e/(1.0+q);
            const double noise=e-signal;
            const double mean=sum[src][env][k]/count[src][env];
            const double g=(signal==0.0||mean==0.0)?0.0:
                fmin(100000.0,sqrt(signal/mean));
            if(!isfinite(signal)||!isfinite(noise)||!isfinite(mean)||
               !isfinite(g)){ok=0;break;}
            next->provisional_gain[t][k]=g;
            next->signal_budget+=signal;
            next->noise_budget+=noise;
            next->raw_power+=p;
            next->missing_source+=(mean==0.0&&signal>0.0)?1u:0u;
            next->harmonic_bins+=slot->harmonic[k]!=0u;
            if(!isfinite(next->signal_budget)||
               !isfinite(next->noise_budget)||
               !isfinite(next->raw_power)){ok=0;break;}
        }
    }
    next->inherited_slots=targets->previous_slots;
    if(ok)*report=*next;
    free(next);
    return ok;
}
