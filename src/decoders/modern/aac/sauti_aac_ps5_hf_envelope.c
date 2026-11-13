#include "sauti_aac_ps5_hf_envelope.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

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

/* PS experimental wideband limiter. This is deliberately NOT normative
 * f_tablelim: it establishes owner-correct gain capping before implementing
 * actual limiter partitions, smoothing and harmonic restoration.
 * Each (source,envelope) group has its own current/desired band energy.
 * The reported gains are finite, PCM-normalized and never published on
 * invalid input. ROM noise is added separately downstream. */
int sauti_aac_ps5_hf_limit_preview(
    const SautiAacSbrTemporalHfTargets *targets,
    const SautiAacSbrComplex *raw,size_t raw_count,
    const SautiAacPs5HfEnvelopeReport *envelope,
    double gains[32u][64u]){
    if(!targets||!raw||!envelope||!gains||raw_count<32u*64u||
       targets->previous_slots>6u)return 0;
    double next[32u][64u]={{0}};
    double target[2u][SAUTI_AAC_SBR_MAX_ENV]={ {0} };
    double current[2u][SAUTI_AAC_SBR_MAX_ENV]={ {0} };
    double post_cap[2u][SAUTI_AAC_SBR_MAX_ENV]={ {0} };
    const double unit=1.0/32768.0;
    const double unit_sq=unit*unit;
    for(unsigned t=0;t<32u;t++){
        const SautiAacSbrTemporalHfSlot *slot=&targets->slot[t];
        const unsigned src=slot->source,env=slot->envelope;
        if(src>1u||env>=SAUTI_AAC_SBR_MAX_ENV||
           (t<targets->previous_slots?src!=0u:src!=1u)||
           slot->kx<1u||slot->kx>32u||slot->k2<=slot->kx||
           slot->k2>64u)return 0;
        for(unsigned k=slot->kx;k<slot->k2;k++){
            const SautiAacSbrComplex x=raw[(size_t)t*64u+k];
            const double e=slot->energy[k],q=slot->noise_factor[k];
            const double g=envelope->provisional_gain[t][k];
            if(!isfinite(x.re)||!isfinite(x.im)||
               !isfinite(e)||e<0.0||!isfinite(q)||q<0.0||
               !isfinite(g)||g<0.0)return 0;
            const double p=(double)x.re*x.re+(double)x.im*x.im;
            target[src][env]+=e/(1.0+q)*unit_sq;
            current[src][env]+=p;
            if(!isfinite(target[src][env])||!isfinite(current[src][env]))
                return 0;
        }
    }
    double caps[2u][SAUTI_AAC_SBR_MAX_ENV]={ {0} };
    for(unsigned src=0;src<2u;src++)
        for(unsigned env=0;env<SAUTI_AAC_SBR_MAX_ENV;env++){
            const double t=target[src][env],c=current[src][env];
            caps[src][env]=(t==0.0||c==0.0)?0.0:
                fmin(100000.0,1.41254*sqrt(t/c));
            if(!isfinite(caps[src][env]))return 0;
        }
    for(unsigned t=0;t<32u;t++){
        const SautiAacSbrTemporalHfSlot *slot=&targets->slot[t];
        const unsigned src=slot->source,env=slot->envelope;
        for(unsigned k=slot->kx;k<slot->k2;k++){
            const SautiAacSbrComplex x=raw[(size_t)t*64u+k];
            const double p=(double)x.re*x.re+(double)x.im*x.im;
            const double gain=fmin(envelope->provisional_gain[t][k]*unit,
                                   caps[src][env]);
            if(!isfinite(gain))return 0;
            next[t][k]=gain;
            post_cap[src][env]+=p*gain*gain;
            if(!isfinite(post_cap[src][env]))return 0;
        }
    }
    double boosts[2u][SAUTI_AAC_SBR_MAX_ENV]={ {0} };
    for(unsigned src=0;src<2u;src++)
        for(unsigned env=0;env<SAUTI_AAC_SBR_MAX_ENV;env++){
            const double t=target[src][env],p=post_cap[src][env];
            boosts[src][env]=(t==0.0||p==0.0)?1.0:
                fmin(1.584893192,sqrt(t/p));
            if(!isfinite(boosts[src][env]))return 0;
        }
    for(unsigned t=0;t<32u;t++){
        const SautiAacSbrTemporalHfSlot *slot=&targets->slot[t];
        const double boost=boosts[slot->source][slot->envelope];
        for(unsigned k=slot->kx;k<slot->k2;k++){
            next[t][k]*=boost;
            if(!isfinite(next[t][k]))return 0;
        }
    }
    memcpy(gains,next,sizeof(next));
    return 1;
}
