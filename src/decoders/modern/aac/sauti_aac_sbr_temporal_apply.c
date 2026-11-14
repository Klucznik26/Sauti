/* SBR-53e: apply whole-envelope gain after an AAC-frame delay.
 * The held HF QMF signal is NEVER published until all of its
 * envelope gains are known, including deferred next-frame tails. */
#include "sauti_aac_sbr_temporal_apply.h"
#include <float.h>
#include <math.h>
#include <string.h>

#define HF_SLOTS 32u
#define HF_BANDS 64u
#define HF_COUNT (HF_SLOTS*HF_BANDS)

static int apply_state_valid(const SautiAacSbrTemporalApplyState *s){
    if(!s || s->ready>1u || s->closed>1u ||
       (s->closed && !s->ready) ||
       (!s->ready && (s->frames || s->pending_slots)) ||
       (s->ready && !s->frames) ||
       s->pending_slots>HF_SLOTS)return 0;
    unsigned unknown=0u;
    for(unsigned t=0u;t<HF_SLOTS;t++){
        if(s->known[t]>1u)return 0;
        if(s->ready && !s->known[t]){
            unknown++;
            if(t<HF_SLOTS-s->pending_slots)return 0;
        }
        if(s->ready && s->known[t] &&
           t>=HF_SLOTS-s->pending_slots)return 0;
        for(unsigned k=0u;k<HF_BANDS;k++){
            const SautiAacSbrComplex x=s->held[(size_t)t*HF_BANDS+k];
            if(!isfinite(x.re) || !isfinite(x.im) ||
               !isfinite(s->slot_gain[t][k]) ||
               s->slot_gain[t][k]<0.0)return 0;
        }
    }
    if(s->ready && unknown!=s->pending_slots)return 0;
    return 1;
}

static int gain_entry_valid(
    const SautiAacSbrTemporalGainEnvelope *g,
    const SautiAacSbrPayloadFrame *f,
    unsigned source,unsigned envelope){
    if(envelope>=f->grid.env_count)return 0;
    const unsigned a=f->env_edges[envelope],z=f->env_edges[envelope+1u];
    const unsigned kx=f->tables.kx,k2=kx+f->tables.m;
    if(a>=z || z>38u || kx<1u || kx>32u || k2>64u ||
       g->source!=source || g->envelope!=envelope ||
       g->slots!=z-a || g->kx!=kx || g->k2!=k2 ||
       g->limiter_count<1u ||
       g->limiter_count>SAUTI_AAC_SBR_MAX_LIMITER_BANDS ||
       g->noise_only_bands>f->tables.m)return 0;
    for(unsigned k=0u;k<HF_BANDS;k++)
        if(!isfinite(g->applied_gain[k]) ||
           g->applied_gain[k]<0.0 ||
           ((k<kx || k>=k2) && g->applied_gain[k]!=0.0))
            return 0;
    return 1;
}

static int gain_report_valid(
    const SautiAacSbrTemporalHistory *history,
    const SautiAacSbrResolvedFrame *owners,
    const SautiAacSbrTemporalGainReport *gains){
    if(gains->completed_count>SAUTI_AAC_SBR_MAX_ENV+1u)return 0;
    unsigned index=0u;
    if(owners->previous_slots){
        const SautiAacSbrPayloadFrame *p=&history->previous;
        if(!history->has_previous || !p->grid.env_count ||
           index>=gains->completed_count ||
           !gain_entry_valid(&gains->completed[index],p,0u,
                             p->grid.env_count-1u))
            return 0;
        index++;
    }
    const SautiAacSbrPayloadFrame *f=&history->current;
    for(unsigned e=0u;e<f->grid.env_count;e++){
        if(f->env_edges[e+1u]>HF_SLOTS){
            if(e+1u!=f->grid.env_count)return 0;
            break;
        }
        if(index>=gains->completed_count ||
           !gain_entry_valid(&gains->completed[index],f,1u,e))
            return 0;
        index++;
    }
    return index==gains->completed_count;
}

static const SautiAacSbrTemporalGainEnvelope *find_gain(
    const SautiAacSbrTemporalGainReport *gains,
    unsigned source,unsigned envelope){
    for(unsigned i=0u;i<gains->completed_count;i++)
        if(gains->completed[i].source==source &&
           gains->completed[i].envelope==envelope)
            return &gains->completed[i];
    return NULL;
}

static int scale_held(
    const SautiAacSbrTemporalApplyState *state,
    SautiAacSbrComplex output[HF_COUNT]){
    for(unsigned t=0u;t<HF_SLOTS;t++){
        if(state->known[t]!=1u)return 0;
        for(unsigned k=0u;k<HF_BANDS;k++){
            const size_t index=(size_t)t*HF_BANDS+k;
            const double g=state->slot_gain[t][k];
            const double re=(double)state->held[index].re*g;
            const double im=(double)state->held[index].im*g;
            if(!isfinite(re) || !isfinite(im) ||
               fabs(re)>FLT_MAX || fabs(im)>FLT_MAX)return 0;
            output[index].re=(float)re;
            output[index].im=(float)im;
        }
    }
    return 1;
}

int sauti_aac_sbr_temporal_gain_apply(
    const SautiAacSbrTemporalHistory *history,
    const SautiAacSbrComplex *baseline_hf,size_t baseline_count,
    const SautiAacSbrTemporalGainReport *gains,
    SautiAacSbrTemporalApplyState *state,
    SautiAacSbrComplex *adjusted_hf,size_t adjusted_count,
    SautiAacSbrTemporalApplyReport *report){
    if(!history || !baseline_hf || !gains || !state ||
       !adjusted_hf || !report || baseline_count<HF_COUNT ||
       adjusted_count<HF_COUNT || !apply_state_valid(state) ||
       state->closed || state->frames==UINT64_MAX ||
       !history->frames || state->frames+1u!=history->frames)
        return 0;

    SautiAacSbrResolvedFrame owners;
    if(!sauti_aac_sbr_temporal_resolve(history,&owners) ||
       !gain_report_valid(history,&owners,gains))
        return 0;
    SautiAacSbrTemporalApplyState next=*state;
    SautiAacSbrComplex scaled[HF_COUNT];
    SautiAacSbrTemporalApplyReport stats={0};
    if(next.ready){
        const SautiAacSbrPayloadFrame *prior=&history->previous;
        const unsigned old_tail=prior->grid.env_count?
            prior->env_edges[prior->grid.env_count]:0u;
        const unsigned old_start=prior->grid.env_count?
            prior->env_edges[prior->grid.env_count-1u]:0u;
        if(!history->has_previous ||
           (next.pending_slots?
             !(old_tail>HF_SLOTS &&
               owners.previous_slots==old_tail-HF_SLOTS &&
               old_start<HF_SLOTS &&
               next.pending_slots==HF_SLOTS-old_start):
             owners.previous_slots!=0u))
            return 0;
        if(next.pending_slots){
            const SautiAacSbrTemporalGainEnvelope *old_gain=
                find_gain(gains,0u,prior->grid.env_count-1u);
            if(!old_gain)return 0;
            for(unsigned t=HF_SLOTS-next.pending_slots;t<HF_SLOTS;t++){
                next.known[t]=1u;
                memcpy(next.slot_gain[t],old_gain->applied_gain,
                       sizeof(next.slot_gain[t]));
            }
            next.pending_slots=0u;
        }
        if(!scale_held(&next,scaled))return 0;
        stats.emitted=1u;
        stats.frame_index=state->frames-1u;
    }else if(owners.previous_slots)
        return 0;

    unsigned pending=0u;
    unsigned tail_started=0u;
    for(unsigned t=0u;t<HF_SLOTS;t++){
        const SautiAacSbrSlotOwner owner=owners.slot[t];
        const SautiAacSbrPayloadFrame *f=
            owner.source==0u?&history->previous:&history->current;
        const SautiAacSbrTemporalGainEnvelope *gain=
            find_gain(gains,owner.source,owner.envelope);
        if(!gain){
            if(owner.source!=1u ||
               owner.envelope+1u!=f->grid.env_count ||
               f->env_edges[f->grid.env_count]<=HF_SLOTS)
                return 0;
            tail_started=1u;
            pending++;
            next.known[t]=0u;
            memset(next.slot_gain[t],0,sizeof(next.slot_gain[t]));
        }else{
            if(tail_started ||
               !gain_entry_valid(gain,f,owner.source,owner.envelope))
                return 0;
            next.known[t]=1u;
            memcpy(next.slot_gain[t],gain->applied_gain,
                   sizeof(next.slot_gain[t]));
        }
    }
    if(pending>HF_SLOTS ||
       (pending && history->current_plan.deferred_slots==0u))
        return 0;
    for(unsigned ix=0u;ix<HF_COUNT;ix++)
        if(!isfinite(baseline_hf[ix].re) ||
           !isfinite(baseline_hf[ix].im))return 0;
    memcpy(next.held,baseline_hf,sizeof(next.held));
    next.pending_slots=(uint8_t)pending;
    next.frames=history->frames;
    next.ready=1u;
    stats.pending_slots=next.pending_slots;

    if(stats.emitted)
        memcpy(adjusted_hf,scaled,sizeof(scaled));
    *state=next;
    *report=stats;
    return 1;
}

int sauti_aac_sbr_temporal_gain_apply_flush(
    SautiAacSbrTemporalApplyState *state,
    SautiAacSbrComplex *adjusted_hf,size_t adjusted_count,
    SautiAacSbrTemporalApplyReport *report){
    if(!state || !adjusted_hf || !report ||
       adjusted_count<HF_COUNT || !apply_state_valid(state) ||
       !state->ready || state->closed || state->pending_slots)
        return 0;
    SautiAacSbrComplex scaled[HF_COUNT];
    if(!scale_held(state,scaled))return 0;
    SautiAacSbrTemporalApplyReport stats={0};
    stats.emitted=1u;
    stats.frame_index=state->frames-1u;
    memcpy(adjusted_hf,scaled,sizeof(scaled));
    state->closed=1u;
    *report=stats;
    return 1;
}

int sauti_aac_sbr_temporal_hf_shift_gain(
    const SautiAacSbrComplex *current,size_t current_count,
    const SautiAacSbrComplex *previous,size_t previous_count,
    const double gain[HF_COUNT],
    const uint8_t apply[HF_COUNT],
    SautiAacSbrComplex *output,size_t output_count){
    if(!current || !gain || !apply || !output ||
       current_count<HF_COUNT || output_count<HF_COUNT)
        return 0;
    if(previous){
        if(previous_count<HF_COUNT)return 0;
    }else if(previous_count)
        return 0;
    SautiAacSbrComplex staging[HF_COUNT];
    memcpy(staging,output,sizeof(staging));
    for(unsigned t=0u;t<HF_SLOTS;t++){
        for(unsigned k=0u;k<HF_BANDS;k++){
            const unsigned ix=t*HF_BANDS+k;
            const unsigned use=apply[ix];
            if(use>1u)return 0;
            if(!use)continue;
            const double g=gain[ix];
            if(!isfinite(g) || g<0.0)return 0;
            if(t<6u && !previous)continue;
            const unsigned src=t<6u?t+26u:t-6u;
            const SautiAacSbrComplex *frame=t<6u?previous:current;
            const SautiAacSbrComplex x=frame[(size_t)src*HF_BANDS+k];
            if(!isfinite(x.re) || !isfinite(x.im))return 0;
            const double re=(double)x.re*g;
            const double im=(double)x.im*g;
            if(!isfinite(re) || !isfinite(im) ||
               fabs(re)>FLT_MAX || fabs(im)>FLT_MAX)
                return 0;
            staging[ix].re=(float)re;
            staging[ix].im=(float)im;
        }
    }
    memcpy(output,staging,sizeof(staging));
    return 1;
}
