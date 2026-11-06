/* SBR-54e: read-only per-bin pre-synthesis SBR power budget audit.
 * Never modify HF, noise, audio, ROM index or temporal state. */
#include "sauti_aac_sbr_temporal_energy_audit.h"
#include <math.h>

#define SLOTS 32u
#define BANDS 64u
#define COUNT (SLOTS*BANDS)

static double abs2(SautiAacSbrComplex v){
    return (double)v.re*(double)v.re+(double)v.im*(double)v.im;
}

int sauti_aac_sbr_temporal_energy_audit(
    const SautiAacSbrTemporalHfTargets *targets,
    const SautiAacSbrComplex *adjusted,size_t adjusted_count,
    const SautiAacSbrTemporalNoiseFrame *noise,
    const SautiAacSbrComplex *mixed,size_t mixed_count,
    const SautiAacSbrTemporalApplyReport *apply,
    const SautiAacSbrTemporalNoiseReport *noise_report,
    SautiAacSbrTemporalEnergyAudit *out){
    if(!targets || !adjusted || !noise || !mixed || !apply ||
       !noise_report || !out || adjusted_count<COUNT ||
       mixed_count<COUNT || apply->emitted!=1u ||
       noise_report->emitted!=1u ||
       apply->frame_index!=noise_report->frame_index ||
       noise_report->vectors>COUNT)return 0;
    SautiAacSbrTemporalEnergyAudit stats={0};
    stats.frame_index=apply->frame_index;
    for(unsigned t=0u;t<SLOTS;t++){
        const SautiAacSbrTemporalHfSlot *owner=&targets->slot[t];
        const unsigned kx=owner->kx,k2=owner->k2;
        if(owner->source>1u || kx<1u || kx>32u ||
           k2<=kx || k2>64u)return 0;
        for(unsigned k=0u;k<BANDS;k++){
            const unsigned ix=t*BANDS+k;
            const unsigned active=k>=kx && k<k2;
            const double e=owner->energy[k];
            const double q=owner->noise_factor[k];
            const double a=noise->raw_amplitude[ix];
            const SautiAacSbrComplex x=adjusted[ix];
            const SautiAacSbrComplex y=mixed[ix];
            if(!isfinite(e) || !isfinite(q) || !isfinite(a) ||
               !isfinite(x.re) || !isfinite(x.im) ||
               !isfinite(y.re) || !isfinite(y.im) ||
               e<0.0 || q<0.0 || a<0.0 ||
               owner->harmonic[k]>1u ||
               noise->active[ix]!=(unsigned)active)return 0;
            const double hp=abs2(x),mp=abs2(y),np=a*a;
            if(!isfinite(hp) || !isfinite(mp) || !isfinite(np))
                return 0;
            if(!active){
                if(e!=0.0 || q!=0.0 || a!=0.0 ||
                   hp!=0.0 || mp!=0.0 ||
                   owner->harmonic[k])return 0;
                continue;
            }
            /* Verify same original decoded target used by SBR-53f. */
            const double expected=e*(q/(1.0+q));
            if(!isfinite(expected) ||
               fabs(np-expected)>1e-6*fmax(1.0,expected))
                return 0;
            stats.active_bins++;
            stats.harmonic_bins+=owner->harmonic[k];
            stats.decoded_target_power+=e;
            stats.adjusted_hf_power+=hp;
            stats.nominal_noise_power+=np;
            stats.mixed_hf_power+=mp;
            if(e>0.0){
                const double ratio=(hp+np)/e;
                if(!isfinite(ratio))return 0;
                if(ratio>1.0001)stats.double_alloc_bins++;
                if(ratio>stats.max_budget_ratio)
                    stats.max_budget_ratio=ratio;
            }
        }
    }
    if(stats.active_bins!=noise_report->vectors ||
       !isfinite(stats.decoded_target_power) ||
       !isfinite(stats.adjusted_hf_power) ||
       !isfinite(stats.nominal_noise_power) ||
       !isfinite(stats.mixed_hf_power) ||
       !isfinite(stats.max_budget_ratio))
        return 0;
    *out=stats;
    return 1;
}
