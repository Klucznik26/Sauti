/* HE-AAC SBR-53d: original-envelope limiter coefficients.
 * The power input already contains ALL QMF slots of each completed
 * envelope, so the gain never depends on a truncated frame window.
 * The validated, deterministic SBR-16 cap/boost model is used;
 * normative noise/sine/smoothing and PCM synthesis are not. */
#include "sauti_aac_sbr_temporal_gain.h"
#include <math.h>
#include <string.h>

static int gain_one(const SautiAacSbrPayloadFrame *f,
                    unsigned source,unsigned e,
                    const SautiAacSbrTemporalPowerEnvelope *power,
                    SautiAacSbrTemporalGainEnvelope *out){
    const SautiAacSbrTables *tab=&f->tables;
    const unsigned kx=tab->kx,k2=kx+tab->m;
    const unsigned start=f->env_edges[e],end=f->env_edges[e+1u];
    if(power->source!=source || power->envelope!=e ||
       power->kx!=kx || power->k2!=k2 ||
       end<=start || end>38u || power->slots!=end-start ||
       f->gain_mode>3u || f->limiter_mode>3u ||
       e>=f->energy.env_count)
        return 0;
    const unsigned fine=f->energy.freq_res[e];
    if(fine>1u)return 0;
    const unsigned n=fine?tab->n_high:tab->n_low;
    const uint8_t *freq=fine?tab->high:tab->low;
    double target[64]={0},current[64]={0},gains[64]={0};
    for(unsigned b=0u;b<n;b++){
        const double etarget=f->energy.env_energy[e][b];
        if(!isfinite(etarget) || etarget<0.0)return 0;
        for(unsigned k=freq[b];k<freq[b+1u];k++)
            target[k]=etarget;
    }

    /* Every original envelope must lie within one noise envelope.
     * Its decoded Q permits noise-only reconstruction when a
     * positive target has no transposed source whatsoever. */
    unsigned noise_index=2u;
    for(unsigned j=0u;j<f->grid.noise_count;j++)
        if(start>=f->noise_edges[j] &&
           end<=f->noise_edges[j+1u]){
            noise_index=j;break;
        }
    if(noise_index>=f->grid.noise_count)return 0;

    unsigned noise_only=0u;
    for(unsigned k=0u;k<64u;k++){
        const double v=power->mean_power[k];
        if(!isfinite(v) || v<0.0 ||
           ((k<kx || k>=k2) && v!=0.0))return 0;
        current[k]=v;
    }
    unsigned noise_band=0u;
    for(unsigned k=kx;k<k2;k++){
        while(noise_band+1u<tab->n_noise &&
              k>=tab->noise[noise_band+1u])noise_band++;
        if(k<tab->noise[noise_band] ||
           k>=tab->noise[noise_band+1u])return 0;
        const double q=f->energy.noise_factor[noise_index][noise_band];
        if(!isfinite(q) || q<0.0)return 0;
        if(target[k]>0.0 && current[k]==0.0){
            if(q==0.0)return 0;
            noise_only++;
        }
    }

    SautiAacSbrLimiterBands bands;
    if(!sauti_aac_sbr_limiter_bands_build(
           tab,&f->patches,f->limiter_mode,&bands))
        return 0;
    if(bands.count>SAUTI_AAC_SBR_MAX_LIMITER_BANDS)return 0;

    static const double limit_gain[4]={
        0.70795,1.0,1.41254,10000000000.0
    };
    const double max_boost=1.584893192;
    SautiAacSbrTemporalGainEnvelope result={0};
    result.source=(uint8_t)source;
    result.envelope=(uint8_t)e;
    result.slots=(uint8_t)(end-start);
    result.kx=(uint8_t)kx;
    result.k2=(uint8_t)k2;
    result.limiter_count=(uint8_t)bands.count;
    result.noise_only_bands=(uint8_t)noise_only;
    for(unsigned group=0u;group<bands.count;group++){
        const unsigned left=bands.edges[group],right=bands.edges[group+1u];
        double sum_target=0.0,sum_current=0.0;
        for(unsigned k=left;k<right;k++){
            sum_target+=target[k];
            sum_current+=current[k];
        }
        if(!isfinite(sum_target) || !isfinite(sum_current) ||
           sum_target<0.0 || sum_current<0.0)return 0;
        double cap=0.0;
        if(sum_target>0.0 && sum_current>0.0)
            cap=fmin(100000.0,
                     limit_gain[f->gain_mode]*
                     (sqrt(sum_target)/sqrt(sum_current)));
        if(!isfinite(cap))return 0;
        result.limiter_max[group]=cap;

        double after=0.0;
        for(unsigned k=left;k<right;k++){
            const double ideal=(target[k]==0.0 || current[k]==0.0)?
                0.0:sqrt(target[k])/sqrt(current[k]);
            gains[k]=fmin(ideal,cap);
            after+=current[k]*gains[k]*gains[k];
        }
        if(!isfinite(after) || after<0.0)return 0;
        const double boost=(sum_target==0.0 || after==0.0)?
            1.0:fmin(max_boost,sqrt(sum_target)/sqrt(after));
        if(!isfinite(boost))return 0;
        result.limiter_boost[group]=boost;
        for(unsigned k=left;k<right;k++){
            gains[k]*=boost;
            if(!isfinite(gains[k]))return 0;
            result.applied_gain[k]=gains[k];
        }
    }
    *out=result;
    return 1;
}

int sauti_aac_sbr_temporal_gain_calculate(
    const SautiAacSbrTemporalHistory *history,
    const SautiAacSbrTemporalPowerReport *power,
    SautiAacSbrTemporalGainReport *out){
    if(!history || !power || !out ||
       power->completed_count>SAUTI_AAC_SBR_MAX_ENV+1u)
        return 0;
    SautiAacSbrTemporalHfTargets owners;
    if(!sauti_aac_sbr_temporal_hf_targets(history,&owners) ||
       power->deferred_slots!=history->current_plan.deferred_slots)
        return 0;
    SautiAacSbrTemporalGainReport next={0};
    unsigned idx=0u;
    if(owners.previous_slots){
        if(!history->has_previous ||
           history->previous.grid.env_count==0u ||
           power->completed_count==0u)return 0;
        const unsigned e=history->previous.grid.env_count-1u;
        if(!gain_one(&history->previous,0u,e,
                     &power->completed[idx],&next.completed[idx]))
            return 0;
        idx++;
    }
    const SautiAacSbrPayloadFrame *f=&history->current;
    for(unsigned e=0u;e<f->grid.env_count;e++){
        if(f->env_edges[e+1u]>32u){
            if(e+1u!=f->grid.env_count)return 0;
            break; /* carries over: limiter needs future HF samples */
        }
        if(idx>=power->completed_count ||
           !gain_one(f,1u,e,&power->completed[idx],
                     &next.completed[idx]))return 0;
        idx++;
    }
    if(idx!=power->completed_count)return 0;
    next.completed_count=(uint8_t)idx;
    *out=next;
    return 1;
}
