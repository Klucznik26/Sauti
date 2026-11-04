/* SBR-53c: exact mean PRE-adjustment HF power across temporal SBR
 * envelope boundaries. No PCM or high-frequency gains are produced. */
#include "sauti_aac_sbr_temporal_power.h"
#include <math.h>
#include <string.h>

static int pending_state_valid(const SautiAacSbrTemporalPowerState *s){
    if(s->ready>1u || s->pending>1u ||
       (!s->ready && (s->frames || s->pending)) ||
       (s->ready && !s->frames))
        return 0;
    if(!s->pending)
        return 1;
    if(s->pending_remaining<1u || s->pending_remaining>6u ||
       s->pending_count<1u || s->pending_total<2u ||
       (unsigned)s->pending_count+s->pending_remaining!=s->pending_total ||
       s->pending_kx<1u || s->pending_kx>32u ||
       s->pending_k2<=s->pending_kx || s->pending_k2>64u)
        return 0;
    for(unsigned k=0u;k<64u;k++)
        if(!isfinite(s->pending_sum[k]) || s->pending_sum[k]<0.0)
            return 0;
    return 1;
}

static int add_qmf_power(const SautiAacSbrTemporalHfTargets *targets,
                         const SautiAacSbrComplex *high,
                         unsigned a,unsigned z,unsigned source,
                         unsigned env,unsigned kx,unsigned k2,
                         double sum[64]){
    for(unsigned t=a;t<z;t++){
        const SautiAacSbrTemporalHfSlot *owner=&targets->slot[t];
        if(owner->source!=source || owner->envelope!=env ||
           owner->kx!=kx || owner->k2!=k2)return 0;
        for(unsigned k=0u;k<64u;k++){
            const SautiAacSbrComplex c=high[(size_t)t*64u+k];
            if(!isfinite(c.re) || !isfinite(c.im))return 0;
            if(k<kx || k>=k2){
                if(c.re!=0.0f || c.im!=0.0f)return 0;
                continue;
            }
            const double re=c.re,im=c.im;
            sum[k]+=re*re+im*im;
            if(!isfinite(sum[k]))return 0;
        }
    }
    return 1;
}

static int publish_envelope(
    SautiAacSbrTemporalPowerReport *report,
    unsigned source,unsigned env,unsigned kx,unsigned k2,
    unsigned count,const double sum[64]){
    if(report->completed_count>=SAUTI_AAC_SBR_MAX_ENV+1u ||
       count<1u || count>38u)return 0;
    SautiAacSbrTemporalPowerEnvelope *out=
        &report->completed[report->completed_count++];
    out->source=(uint8_t)source;
    out->envelope=(uint8_t)env;
    out->kx=(uint8_t)kx;
    out->k2=(uint8_t)k2;
    out->slots=(uint8_t)count;
    for(unsigned k=kx;k<k2;k++){
        out->mean_power[k]=sum[k]/(double)count;
        if(!isfinite(out->mean_power[k]) ||
           out->mean_power[k]<0.0)return 0;
    }
    return 1;
}

int sauti_aac_sbr_temporal_power_measure(
    const SautiAacSbrTemporalHistory *history,
    const SautiAacSbrComplex *baseline_hf,size_t hf_count,
    SautiAacSbrTemporalPowerState *state,
    SautiAacSbrTemporalPowerReport *report){
    if(!history || !baseline_hf || !state || !report ||
       hf_count<32u*64u || !pending_state_valid(state) ||
       !history->frames || state->frames==UINT64_MAX ||
       state->frames+1u!=history->frames)
        return 0;

    SautiAacSbrTemporalHfTargets targets;
    if(!sauti_aac_sbr_temporal_hf_targets(history,&targets) ||
       (state->pending?
           targets.previous_slots!=state->pending_remaining:
           targets.previous_slots!=0u))
        return 0;

    SautiAacSbrTemporalPowerState next=*state;
    SautiAacSbrTemporalPowerReport result={0};
    unsigned t=0u;
    while(t<32u){
        const SautiAacSbrTemporalHfSlot *owner=&targets.slot[t];
        const unsigned source=owner->source,env=owner->envelope;
        const SautiAacSbrPayloadFrame *frame=
            source==0u?&history->previous:&history->current;
        if(source>1u || env>=frame->grid.env_count)return 0;
        const unsigned a=t;
        while(t<32u &&
              targets.slot[t].source==source &&
              targets.slot[t].envelope==env)t++;
        const unsigned z=t;
        const unsigned kx=owner->kx,k2=owner->k2;
        const unsigned orig_start=frame->env_edges[env];
        const unsigned orig_end=frame->env_edges[env+1u];
        if(orig_end<=orig_start || orig_end>38u)return 0;

        if(source==0u){
            if(a!=0u || !next.pending ||
               env!=next.pending_envelope ||
               z!=next.pending_remaining ||
               orig_start>=32u ||
               orig_end-32u!=next.pending_remaining ||
               orig_end-orig_start!=next.pending_total ||
               next.pending_kx!=kx || next.pending_k2!=k2)
                return 0;
            if(!add_qmf_power(&targets,baseline_hf,a,z,source,
                              env,kx,k2,next.pending_sum) ||
               !publish_envelope(&result,0u,env,kx,k2,
                    next.pending_total,next.pending_sum))
                return 0;
            next.pending=0u;
            next.pending_remaining=0u;
            next.pending_count=0u;
            next.pending_total=0u;
            next.pending_envelope=0u;
            next.pending_kx=0u;next.pending_k2=0u;
            memset(next.pending_sum,0,sizeof(next.pending_sum));
        }else{
            if(a!=orig_start ||
               z!=(orig_end<32u?orig_end:32u) ||
               next.pending)return 0;
            double sum[64]={0};
            if(!add_qmf_power(&targets,baseline_hf,a,z,source,
                              env,kx,k2,sum))return 0;
            if(orig_end>32u){
                if(z!=32u || env+1u!=frame->grid.env_count ||
                   orig_end-32u>6u)return 0;
                next.pending=1u;
                next.pending_envelope=(uint8_t)env;
                next.pending_kx=(uint8_t)kx;
                next.pending_k2=(uint8_t)k2;
                next.pending_count=(uint8_t)(z-a);
                next.pending_total=(uint8_t)(orig_end-orig_start);
                next.pending_remaining=(uint8_t)(orig_end-32u);
                memcpy(next.pending_sum,sum,sizeof(sum));
            }else if(!publish_envelope(&result,1u,env,kx,k2,
                                       z-a,sum))
                return 0;
        }
    }
    next.ready=1u;
    next.frames=history->frames;
    result.deferred_slots=next.pending?next.pending_remaining:0u;
    *state=next;
    *report=result;
    return 1;
}
