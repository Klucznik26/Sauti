/* SBR-54m: gain-G ONLY adapter for isolated, delayed HF PCM A/B.
 * No modification of existing SBR-53d, SBR-53e or playback state. */
#include "sauti_aac_sbr_temporal_gain_bridge.h"
#include <math.h>
#include <string.h>

int sauti_aac_sbr_temporal_gain_bridge(
    const SautiAacSbrTemporalReferenceGainReport *reference,
    SautiAacSbrTemporalGainReport *out){
    if(!reference || !out ||
       reference->completed_count>SAUTI_AAC_SBR_MAX_ENV+1u)
        return 0;
    SautiAacSbrTemporalGainReport next={0};
    next.completed_count=reference->completed_count;
    for(unsigned e=0;e<reference->completed_count;e++){
        const SautiAacSbrTemporalReferenceGainEnvelope *r=
            &reference->completed[e];
        SautiAacSbrTemporalGainEnvelope *g=&next.completed[e];
        if(r->source>1u || r->envelope>=SAUTI_AAC_SBR_MAX_ENV ||
           r->slots<1u || r->slots>38u ||
           r->kx<1u || r->kx>32u || r->k2<=r->kx || r->k2>64u ||
           r->limiter_count<1u ||
           r->limiter_count>SAUTI_AAC_SBR_MAX_LIMITER_BANDS ||
           r->limited.groups!=r->limiter_count)return 0;
        g->source=r->source;g->envelope=r->envelope;g->slots=r->slots;
        g->kx=r->kx;g->k2=r->k2;
        g->limiter_count=r->limiter_count;
        for(unsigned j=0;j<g->limiter_count;j++){
            const double cap=r->limited.group_cap[j];
            const double boost=r->limited.group_boost[j];
            if(!isfinite(cap) || !isfinite(boost) ||
               cap<0.0 || boost<0.0)return 0;
            g->limiter_max[j]=cap;
            g->limiter_boost[j]=boost;
        }
        for(unsigned k=0;k<64u;k++){
            const SautiAacSbrGainPartition *p=&r->limited.band[k];
            if(!isfinite(p->hf_gain) ||
               !isfinite(p->noise_amplitude) ||
               !isfinite(p->sine_amplitude) ||
               p->hf_gain<0.0 || p->noise_amplitude<0.0 ||
               p->sine_amplitude<0.0 ||
               ((k<r->kx || k>=r->k2) &&
                (p->hf_gain!=0.0 || p->noise_amplitude!=0.0 ||
                 p->sine_amplitude!=0.0)))return 0;
            g->applied_gain[k]=p->hf_gain;
        }
    }
    *out=next;
    return 1;
}
