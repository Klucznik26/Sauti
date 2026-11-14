#include "sauti_aac_ps5_hf_gain.h"
#include <math.h>
#include <stdlib.h>

/* Side-data driven QMF diagnostic, not an SBR HF rendering substitute.
 * Per-slot power is not envelope-averaged power. This calculation is
 * intentionally separate from the production SBR limiter. */
int sauti_aac_ps5_hf_gain_analyze(
    const SautiAacSbrTemporalHfTargets *targets,
    const SautiAacSbrComplex *raw,size_t raw_count,
    SautiAacPs5HfGainReport *report){
    if(!targets||!raw||!report||raw_count<32u*64u||
       targets->previous_slots>6u)return 0;
    SautiAacPs5HfGainReport *next=calloc(1u,sizeof(*next));
    if(!next)return 0;
    next->inherited_slots=targets->previous_slots;
    int ok=1;
    for(unsigned t=0;t<32u&&ok;t++){
        const SautiAacSbrTemporalHfSlot *slot=&targets->slot[t];
        if(slot->source>1u||slot->kx==0u||slot->kx>32u||
           slot->k2<=slot->kx||slot->k2>64u||
           (t<targets->previous_slots?slot->source!=0u:
                                         slot->source!=1u)||
           slot->smooth_bypass>1u){ok=0;break;}
        for(unsigned k=slot->kx;k<slot->k2;k++){
            const SautiAacSbrComplex x=raw[t*64u+k];
            const double e=slot->energy[k],n=slot->noise_factor[k];
            if(!isfinite(x.re)||!isfinite(x.im)||
               !isfinite(e)||e<0.0||!isfinite(n)||n<0.0||
               slot->harmonic[k]>1u){ok=0;break;}
            const double p=(double)x.re*x.re+(double)x.im*x.im;
            if(!isfinite(p)){ok=0;break;}
            const double g=(p==0.0||e==0.0)?0.0:
                           fmin(100000.0,sqrt(e/p));
            if(!isfinite(g)){ok=0;break;}
            next->gain[t][k]=g;
            next->missing_source+=(p==0.0&&e>0.0)?1u:0u;
            next->raw_power+=p;
            next->target_power+=e;
            next->noise_target+=e*(n/(1.0+n));
            next->predicted_signal_power+=p*g*g;
            if(!isfinite(next->raw_power)||
               !isfinite(next->target_power)||
               !isfinite(next->noise_target)||
               !isfinite(next->predicted_signal_power)){ok=0;break;}
        }
    }
    if(ok)*report=*next;
    free(next);
    return ok;
}
