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

/* Experimental PS-5 per-owner f_tablelim gain cap.
 * Owners are never mixed, even when they share an envelope index or
 * physical QMF bin. The ROM noise and tonal components are separate.
 * This diagnostic uses target signal energy, not the complete normative
 * noise+harmonic energy-restoration equation. */
int sauti_aac_ps5_hf_limit_bands_preview(
    const SautiAacSbrTemporalHfTargets *targets,
    const SautiAacSbrComplex *raw,size_t raw_count,
    const SautiAacPs5HfEnvelopeReport *envelope,
    const uint8_t edges[2u][65u],const unsigned edge_count[2u],
    const unsigned gain_mode[2u],
    double gains[32u][64u]){
    if(!targets||!raw||!envelope||!edges||!edge_count||!gain_mode||
       !gains||raw_count<32u*64u||targets->previous_slots>6u)
        return 0;
    /* Bounded 2 owner x 5 envelope x 64 limiter-band accumulators. */
    double desired[2u][SAUTI_AAC_SBR_MAX_ENV][64u]={{{0}}};
    double measured[2u][SAUTI_AAC_SBR_MAX_ENV][64u]={{{0}}};
    double restored[2u][SAUTI_AAC_SBR_MAX_ENV][64u]={{{0}}};
    double cap[2u][SAUTI_AAC_SBR_MAX_ENV][64u]={{{0}}};
    double boost[2u][SAUTI_AAC_SBR_MAX_ENV][64u]={{{0}}};
    double next[32u][64u]={{0}};
    const double unit=1.0/32768.0;
    static const double lim_gain[4u]={0.70795,1.0,1.41254,100000.0};
    unsigned active_source[2u]={0u,0u};
    unsigned seen_kx[2u]={0u,0u},seen_k2[2u]={0u,0u};
    for(unsigned src=0;src<2u;src++){
        if(gain_mode[src]>3u||edge_count[src]<1u||edge_count[src]>64u)
            return 0;
        for(unsigned b=0;b<edge_count[src];b++)
            if(edges[src][b]>=edges[src][b+1u]||
               edges[src][b]<1u||edges[src][b+1u]>64u)
                return 0;
    }
    for(unsigned t=0;t<32u;t++){
        const SautiAacSbrTemporalHfSlot *slot=&targets->slot[t];
        const unsigned src=slot->source,env=slot->envelope;
        if(src>1u||env>=SAUTI_AAC_SBR_MAX_ENV||
           (t<targets->previous_slots?src!=0u:src!=1u)||
           slot->kx<1u||slot->kx>32u||slot->k2<=slot->kx||
           slot->k2>64u)return 0;
        if(!active_source[src]){
            seen_kx[src]=slot->kx;seen_k2[src]=slot->k2;
        }
        active_source[src]++;
        if(seen_kx[src]!=slot->kx||seen_k2[src]!=slot->k2)
            return 0;
        if(edges[src][0]!=slot->kx||
           edges[src][edge_count[src]]!=slot->k2)
            return 0;
        unsigned band=0u;
        for(unsigned k=slot->kx;k<slot->k2;k++){
            while(band+1u<edge_count[src] && k>=edges[src][band+1u])
                band++;
            if(k<edges[src][band]||k>=edges[src][band+1u])
                return 0;
            const SautiAacSbrComplex x=raw[(size_t)t*64u+k];
            const double e=slot->energy[k],q=slot->noise_factor[k];
            const double g=envelope->provisional_gain[t][k];
            if(!isfinite(x.re)||!isfinite(x.im)||!isfinite(e)||
               e<0.0||!isfinite(q)||q<0.0||
               !isfinite(g)||g<0.0)return 0;
            const double re=x.re,im=x.im;
            const double p=re*re+im*im;
            desired[src][env][band]+=e/(1.0+q)*unit*unit;
            measured[src][env][band]+=p;
            if(!isfinite(desired[src][env][band])||
               !isfinite(measured[src][env][band]))return 0;
        }
    }
    for(unsigned src=0;src<2u;src++)
        for(unsigned env=0;env<SAUTI_AAC_SBR_MAX_ENV;env++)
            for(unsigned b=0;b<edge_count[src];b++){
                const double d=desired[src][env][b];
                const double m=measured[src][env][b];
                cap[src][env][b]=(d==0.0||m==0.0)?0.0:
                    fmin(100000.0,lim_gain[gain_mode[src]]*sqrt(d/m));
                if(!isfinite(cap[src][env][b]))return 0;
            }
    for(unsigned t=0;t<32u;t++){
        const SautiAacSbrTemporalHfSlot *slot=&targets->slot[t];
        const unsigned src=slot->source,env=slot->envelope;
        unsigned b=0u;
        for(unsigned k=slot->kx;k<slot->k2;k++){
            while(b+1u<edge_count[src]&&k>=edges[src][b+1u])b++;
            const SautiAacSbrComplex x=raw[(size_t)t*64u+k];
            const double gain=fmin(envelope->provisional_gain[t][k]*unit,
                                   cap[src][env][b]);
            const double power=(double)x.re*x.re+(double)x.im*x.im;
            if(!isfinite(gain)||gain<0.0)return 0;
            next[t][k]=gain;
            restored[src][env][b]+=power*gain*gain;
            if(!isfinite(restored[src][env][b]))return 0;
        }
    }
    for(unsigned src=0;src<2u;src++)
        for(unsigned env=0;env<SAUTI_AAC_SBR_MAX_ENV;env++)
            for(unsigned b=0;b<edge_count[src];b++){
                const double d=desired[src][env][b];
                const double m=restored[src][env][b];
                boost[src][env][b]=(d==0.0||m==0.0)?1.0:
                    fmin(1.584893192,sqrt(d/m));
                if(!isfinite(boost[src][env][b]))return 0;
            }
    for(unsigned t=0;t<32u;t++){
        const SautiAacSbrTemporalHfSlot *slot=&targets->slot[t];
        const unsigned src=slot->source,env=slot->envelope;
        unsigned b=0u;
        for(unsigned k=slot->kx;k<slot->k2;k++){
            while(b+1u<edge_count[src]&&k>=edges[src][b+1u])b++;
            next[t][k]*=boost[src][env][b];
            if(!isfinite(next[t][k]))return 0;
        }
    }
    memcpy(gains,next,sizeof(next));
    return 1;
}

/* PS experimental energy balancing. Use existing f_tablelim owner
 * caps, then account for decoded SBR noise budget when restoring
 * group energy. ROM has assumed unit expected complex magnitude.
 * This is not normative SBR including sine/harmonic restoration. */
int sauti_aac_ps5_hf_energy_balance_preview(
    const SautiAacSbrTemporalHfTargets *targets,
    const SautiAacSbrComplex *raw,size_t raw_count,
    const SautiAacPs5HfEnvelopeReport *envelope,
    const uint8_t edges[2u][65u],const unsigned edge_count[2u],
    const unsigned gain_mode[2u],
    double signal_gains[32u][64u],double noise_amplitudes[32u][64u]){
    if(!targets||!raw||!envelope||!edges||!edge_count||!gain_mode||
       !signal_gains||!noise_amplitudes||signal_gains==noise_amplitudes)
        return 0;
    double next_signal[32u][64u];
    double next_noise[32u][64u]={{0}};
    if(!sauti_aac_ps5_hf_limit_bands_preview(
           targets,raw,raw_count,envelope,edges,edge_count,
           gain_mode,next_signal))return 0;
    double desired[2u][SAUTI_AAC_SBR_MAX_ENV][64u]={{{0}}};
    double available[2u][SAUTI_AAC_SBR_MAX_ENV][64u]={{{0}}};
    double correction[2u][SAUTI_AAC_SBR_MAX_ENV][64u]={{{0}}};
    const double unit=1.0/32768.0;
    for(unsigned t=0;t<32u;t++){
        const SautiAacSbrTemporalHfSlot *slot=&targets->slot[t];
        const unsigned src=slot->source,env=slot->envelope;
        unsigned b=0u;
        for(unsigned k=slot->kx;k<slot->k2;k++){
            while(b+1u<edge_count[src] && k>=edges[src][b+1u])b++;
            const double e=slot->energy[k],q=slot->noise_factor[k];
            const double n=e*(q/(1.0+q))*unit*unit;
            const double g=next_signal[t][k];
            const SautiAacSbrComplex x=raw[(size_t)t*64u+k];
            const double signal=((double)x.re*x.re+(double)x.im*x.im)*g*g;
            if(!isfinite(n)||n<0.0||!isfinite(signal))return 0;
            next_noise[t][k]=sqrt(n);
            desired[src][env][b]+=e*unit*unit;
            available[src][env][b]+=signal+n;
            if(!isfinite(desired[src][env][b])||
               !isfinite(available[src][env][b]))return 0;
        }
    }
    for(unsigned src=0;src<2u;src++)
        for(unsigned env=0;env<SAUTI_AAC_SBR_MAX_ENV;env++)
            for(unsigned b=0;b<edge_count[src];b++){
                const double wanted=desired[src][env][b];
                const double have=available[src][env][b];
                /* Never invent energy for a zero-signal, zero-noise group.
                 * The existing 4 dB ceiling is preserved. */
                correction[src][env][b]=(wanted==0.0||have==0.0)?1.0:
                    fmin(1.584893192,sqrt(wanted/have));
                if(!isfinite(correction[src][env][b])||
                   correction[src][env][b]<0.0)return 0;
            }
    for(unsigned t=0;t<32u;t++){
        const SautiAacSbrTemporalHfSlot *slot=&targets->slot[t];
        const unsigned src=slot->source,env=slot->envelope;
        unsigned b=0u;
        for(unsigned k=slot->kx;k<slot->k2;k++){
            while(b+1u<edge_count[src] && k>=edges[src][b+1u])b++;
            const double f=correction[src][env][b];
            next_signal[t][k]*=f;
            next_noise[t][k]*=f;
            if(!isfinite(next_signal[t][k])||
               !isfinite(next_noise[t][k]))return 0;
        }
    }
    memcpy(signal_gains,next_signal,sizeof(next_signal));
    memcpy(noise_amplitudes,next_noise,sizeof(next_noise));
    return 1;
}
