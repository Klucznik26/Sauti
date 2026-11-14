#include "sauti_aac_ps5_cross.h"
#include <math.h>
#include <string.h>

/* The previous LAST envelope is [start, end), with end=32+tail.
 * The first 'tail' slots in current belong to this same envelope.
 * Unlike per-frame aggregation, we average across the full source
 * support. No QMF sample or decoder state is mutated. */
int sauti_aac_ps5_cross_envelope_analyze(
    const SautiAacSbrTemporalHistory *history,
    const SautiAacSbrTemporalHfTargets *targets,
    const SautiAacSbrComplex *old,size_t old_count,
    const SautiAacSbrComplex *now,size_t now_count,
    SautiAacPs5CrossReport *report){
    if(!history||!targets||!old||!now||!report||
       old_count<32u*64u||now_count<32u*64u||
       history->ready!=1u||history->has_previous!=1u)return 0;
    const SautiAacSbrPayloadFrame *p=&history->previous;
    const unsigned env_count=p->grid.env_count;
    if(env_count==0u||env_count>SAUTI_AAC_SBR_MAX_ENV)return 0;
    const unsigned tail=history->previous_plan.deferred_slots;
    const unsigned start=p->env_edges[env_count-1u];
    const unsigned end=p->env_edges[env_count];
    if(tail==0u||tail>6u||end!=32u+tail||
       start>=32u||targets->previous_slots!=tail||
       p->tables.kx<1u||p->tables.kx>32u||
       p->tables.kx+p->tables.m>64u||p->tables.m==0u)return 0;
    const unsigned previous_slots=32u-start;
    const unsigned total=previous_slots+tail;
    if(total!=end-start)return 0;
    SautiAacPs5CrossReport next={0};
    next.deferred_slots=tail;
    next.previous_slots_in_envelope=previous_slots;
    next.total_slots_in_envelope=total;
    /* The inherited slot's original target must match the previous
     * frame's last envelope across all deferred time samples. */
    for(unsigned t=0u;t<tail;t++){
        const SautiAacSbrTemporalHfSlot *slot=&targets->slot[t];
        if(slot->source!=0u||slot->envelope!=env_count-1u||
           slot->kx!=p->tables.kx||
           slot->k2!=p->tables.kx+p->tables.m)return 0;
    }
    for(unsigned k=p->tables.kx;k<p->tables.kx+p->tables.m;k++){
        const SautiAacSbrTemporalHfSlot *slot=&targets->slot[0];
        const double e=slot->energy[k],q=slot->noise_factor[k];
        if(!isfinite(e)||e<0.0||!isfinite(q)||q<0.0||
           slot->harmonic[k]>1u)return 0;
        double sum=0.0;
        for(unsigned t=start;t<32u;t++){
            const SautiAacSbrComplex c=old[(size_t)t*64u+k];
            if(!isfinite(c.re)||!isfinite(c.im))return 0;
            const double z=(double)c.re*c.re+(double)c.im*c.im;
            sum+=z;
            if(!isfinite(sum))return 0;
        }
        for(unsigned t=0u;t<tail;t++){
            const SautiAacSbrTemporalHfSlot *owned=&targets->slot[t];
            if(owned->energy[k]!=e||
               owned->noise_factor[k]!=q||
               owned->harmonic[k]!=slot->harmonic[k])return 0;
            const SautiAacSbrComplex c=now[(size_t)t*64u+k];
            if(!isfinite(c.re)||!isfinite(c.im))return 0;
            sum+=(double)c.re*c.re+(double)c.im*c.im;
            if(!isfinite(sum))return 0;
        }
        const double mean=sum/total;
        const double signal=e/(1.0+q), noise=e-signal;
        const double g=(signal==0.0||mean==0.0)?0.0:
                         fmin(100000.0,sqrt(signal/mean));
        if(!isfinite(signal)||!isfinite(noise)||!isfinite(g))return 0;
        next.mean_source_power[k]=mean;
        next.provisional_gain[k]=g;
        next.missing_source_bins+=(mean==0.0&&signal>0.0)?1u:0u;
        next.harmonic_bins+=slot->harmonic[k]!=0u;
        next.signal_budget+=signal*total;
        next.noise_budget+=noise*total;
        if(!isfinite(next.signal_budget)||
           !isfinite(next.noise_budget))return 0;
    }
    *report=next;
    return 1;
}
