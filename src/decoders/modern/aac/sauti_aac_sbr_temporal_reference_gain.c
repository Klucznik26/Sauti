/* SBR-54l: compare original-envelope SBR-53d with the separately
 * verified 54j/54k gain/noise/sine limiter on REAL HF power.
 *
 * NO audible PCM is modified, including for FIXVAR carry-over.
 * Envelope energy and the current arrive already divided by 2^30.
 * This call lifts only those two arrays back to the raw domain
 * the limiter's additive 1 expects. The current is the HF-generator
 * window when the owner frame is available, otherwise the unshifted
 * mean. Q is not scaled. Square sums stay on that unshifted mean.
 * The high-band midpoint and the envelope-band spread are separate
 * sine maps. Cross-frame e_a remains pending. SBR-53d is not
 * retargeted. */
#include "sauti_aac_sbr_temporal_reference_gain.h"
#include <math.h>
#include <string.h>

static int build_one(
    const SautiAacSbrPayloadFrame *f,
    const SautiAacSbrTemporalPowerEnvelope *power,
    const SautiAacSbrTemporalGainEnvelope *legacy,
    unsigned source,
    const SautiAacSbrComplex *grid,size_t grid_count,
    const SautiAacSbrComplex *older,size_t older_count,
    SautiAacSbrTemporalReferenceGainEnvelope *out){
    if(!f || !power || !legacy || !out ||
       power->source!=source || legacy->source!=source ||
       power->envelope!=legacy->envelope ||
       power->slots!=legacy->slots ||
       power->kx!=legacy->kx || power->k2!=legacy->k2 ||
       legacy->limiter_count==0u ||
       legacy->limiter_count>64u ||
       power->envelope>=f->grid.env_count ||
       f->transient_env>f->grid.env_count ||
       f->energy.env_count!=f->grid.env_count ||
       f->energy.noise_count!=f->grid.noise_count)
        return 0;
    const unsigned env=power->envelope;
    const SautiAacSbrTables *tab=&f->tables;
    const unsigned kx=tab->kx,k2=kx+tab->m;
    if(kx!=power->kx || k2!=power->k2 ||
       kx<1u || kx>32u || k2>64u ||
       f->energy.freq_res[env]>1u)return 0;
    SautiAacSbrLimiterBands bands;
    if(!sauti_aac_sbr_limiter_bands_build(
           tab,&f->patches,f->limiter_mode,&bands) ||
       bands.count!=legacy->limiter_count)return 0;
    const unsigned lo=f->env_edges[env],hi=f->env_edges[env+1u];
    if(hi<=lo || hi>38u)return 0;
    unsigned noise_idx=f->grid.noise_count;
    for(unsigned j=0;j<f->grid.noise_count;j++)
        if(lo>=f->noise_edges[j] && hi<=f->noise_edges[j+1u]){
            noise_idx=j;break;
        }
    if(noise_idx>=f->grid.noise_count)return 0;
    double e[64]={0},q[64]={0},curr[64]={0};
    uint8_t active_sine[64]={0},sine_mapped[64]={0};
    const unsigned fine=f->energy.freq_res[env];
    const uint8_t *freq=fine?tab->high:tab->low;
    const unsigned n=fine?tab->n_high:tab->n_low;
    for(unsigned b=0;b<n;b++){
        if(freq[b]<kx || freq[b+1u]>k2 ||
           freq[b]>=freq[b+1u])return 0;
        const double v=f->energy.env_energy[env][b];
        if(!isfinite(v) || v<0.0)return 0;
        for(unsigned k=freq[b];k<freq[b+1u];k++)
            e[k]=v;
    }
    for(unsigned b=0;b<tab->n_noise;b++){
        if(tab->noise[b]<kx ||
           tab->noise[b+1u]>k2 ||
           tab->noise[b]>=tab->noise[b+1u])return 0;
        const double v=f->energy.noise_factor[noise_idx][b];
        if(!isfinite(v) || v<0.0)return 0;
        for(unsigned k=tab->noise[b];k<tab->noise[b+1u];k++)
            q[k]=v;
    }
    for(unsigned b=0;b<tab->n_high;b++){
        if(f->harmonic[b]>1u ||
           tab->high[b]<kx ||
           tab->high[b+1u]>k2 ||
           tab->high[b]>=tab->high[b+1u])return 0;
        if(f->harmonic[b]){
            const unsigned center=(tab->high[b]+tab->high[b+1u])/2u;
            active_sine[center]=1u;
        }
    }
    /* Spread each midpoint across its envelope-resolution band.
     * That is FFmpeg s_mapped. The midpoint itself stays s_index. */
    for(unsigned b=0;b<n;b++){
        unsigned present=0u;
        for(unsigned k=freq[b];k<freq[b+1u];k++)
            if(active_sine[k])present=1u;
        if(!present)continue;
        for(unsigned k=freq[b];k<freq[b+1u];k++)
            sine_mapped[k]=1u;
    }
    double mean_curr[64]={0};
    for(unsigned k=kx;k<k2;k++){
        const double v=power->mean_power[k];
        if(!isfinite(v) || v<0.0 ||
           !isfinite(legacy->applied_gain[k]) ||
           legacy->applied_gain[k]<0.0)return 0;
        mean_curr[k]=v;
        curr[k]=v;
    }
    /* Slots before the owner frame need the frame before it.
     * Startup leaves those envelopes on the unshifted mean. */
    if(grid && !((int)lo-6<0 && !older)){
        double window[64];
        if(!sauti_aac_sbr_temporal_power_hfgen_window(
               grid,grid_count,older,older_count,
               lo,hi,kx,k2,window))return 0;
        for(unsigned k=kx;k<k2;k++){
            if(!isfinite(window[k]) || window[k]<0.0)return 0;
            curr[k]=window[k];
        }
    }
    /* delta=0 marks the locally signaled transient envelope.
     * FFmpeg's cross-frame e_a history is not yet represented.
     * Square sums below stay on the unscaled mean power. */
    double raw_e[64]={0},raw_c[64]={0};
    for(unsigned k=kx;k<k2;k++){
        raw_e[k]=ldexp(e[k],30);
        raw_c[k]=ldexp(curr[k],30);
        if(!isfinite(raw_e[k]) || raw_e[k]<0.0 ||
           !isfinite(raw_c[k]) || raw_c[k]<0.0)return 0;
    }
    const unsigned delta=env==f->transient_env?0u:1u;
    SautiAacSbrGainLimitedFrame limited;
    if(!sauti_aac_sbr_gain_limit_partition_flags(
           raw_e,raw_c,q,active_sine,sine_mapped,
           bands.edges,bands.count,
           f->gain_mode,delta,&limited))return 0;
    SautiAacSbrTemporalReferenceGainEnvelope value={0};
    value.source=(uint8_t)source;
    value.envelope=(uint8_t)env;
    value.slots=power->slots;
    value.kx=(uint8_t)kx;value.k2=(uint8_t)k2;
    value.limiter_count=(uint8_t)bands.count;
    value.transient_delta=(uint8_t)delta;
    value.limited=limited;
    for(unsigned k=kx;k<k2;k++){
        const double old=legacy->applied_gain[k];
        const double new_gain=limited.band[k].hf_gain;
        value.old_gain_sq_sum+=old*old*mean_curr[k];
        value.new_gain_sq_sum+=new_gain*new_gain*mean_curr[k];
        value.harmonic_bins+=active_sine[k];
    }
    if(!isfinite(value.old_gain_sq_sum) ||
       !isfinite(value.new_gain_sq_sum))return 0;
    *out=value;
    return 1;
}

int sauti_aac_sbr_temporal_reference_gain_calculate(
    const SautiAacSbrTemporalHistory *history,
    const SautiAacSbrTemporalPowerReport *power,
    const SautiAacSbrComplex *current_hf,size_t current_count,
    const SautiAacSbrComplex *previous_hf,size_t previous_count,
    const SautiAacSbrComplex *older_hf,size_t older_count,
    SautiAacSbrTemporalReferenceGainReport *out){
    if(!history || !power || !out ||
       !history->ready ||
       power->completed_count>SAUTI_AAC_SBR_MAX_ENV+1u)return 0;
    SautiAacSbrTemporalGainReport baseline;
    if(!sauti_aac_sbr_temporal_gain_calculate(
           history,power,&baseline) ||
       baseline.completed_count!=power->completed_count)return 0;
    SautiAacSbrTemporalReferenceGainReport next={0};
    next.completed_count=power->completed_count;
    for(unsigned i=0;i<power->completed_count;i++){
        const SautiAacSbrTemporalPowerEnvelope *p=&power->completed[i];
        if(p->source>1u ||
           (p->source==0u && !history->has_previous))return 0;
        const SautiAacSbrPayloadFrame *f=
            p->source==0u?&history->previous:&history->current;
        SautiAacSbrTemporalReferenceGainEnvelope *row=
            &next.completed[i];
        const SautiAacSbrComplex *grid=
            p->source==0u?previous_hf:current_hf;
        const size_t grid_count=
            p->source==0u?previous_count:current_count;
        const SautiAacSbrComplex *older=
            p->source==0u?older_hf:previous_hf;
        const size_t older_n=
            p->source==0u?older_count:previous_count;
        if(!build_one(f,p,&baseline.completed[i],p->source,
                      grid,grid_count,older,older_n,row))
            return 0;
        next.group_count+=row->limited.groups;
        next.noise_capped_bins+=row->limited.noise_capped_bins;
        next.sine_bins+=row->limited.sine_bins;
        next.old_gain_sq_sum+=row->old_gain_sq_sum;
        next.new_gain_sq_sum+=row->new_gain_sq_sum;
        for(unsigned k=row->kx;k<row->k2;k++){
            const double a=baseline.completed[i].applied_gain[k];
            const double b=row->limited.band[k].hf_gain;
            const double tolerance=1e-7*fmax(1.0,fmax(a,b));
            next.gain_changed_bins+=(fabs(a-b)>tolerance);
            next.noise_positive_bins+=(
                row->limited.band[k].noise_amplitude>0.0);
        }
    }
    if(!isfinite(next.old_gain_sq_sum) ||
       !isfinite(next.new_gain_sq_sum))return 0;
    *out=next;
    return 1;
}
