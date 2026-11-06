/* SBR-54s: six-slot lookback source QMF; does NOT modify decoder.
 * Source indexing is FFmpeg W[i+2-8] = W[i-6], prior to numerical
 * QMF conformance and independent of original FIXVAR side-data owners.
 */
#include "sauti_aac_sbr_temporal_source6.h"
#include <math.h>
#include <string.h>

#define LOW_SLOTS  (32u*32u)
#define HIGH_SLOTS (32u*64u)
#define CARRY      (6u*32u)

static int state_valid(const SautiAacSbrTemporalSource6State *state){
    if(!state || state->ready>1u ||
       (state->ready && state->frames==0u) ||
       (!state->ready && state->frames!=0u) ||
       state->frames==UINT64_MAX)return 0;
    for(unsigned i=0u;i<CARRY;i++){
        const SautiAacSbrComplex z=state->previous_last6[i];
        if(!isfinite(z.re) || !isfinite(z.im) ||
           (!state->ready && (z.re!=0.0f || z.im!=0.0f)))
            return 0;
    }
    return 1;
}

int sauti_aac_sbr_temporal_source6_prepare(
    const SautiAacSbrTemporalHistory *history,
    const SautiAacSbrComplex *current_low,size_t current_count,
    SautiAacSbrTemporalSource6State *state,
    SautiAacSbrComplex *aligned_low,size_t aligned_count,
    SautiAacSbrComplex *aligned_high,size_t high_count,
    SautiAacSbrTemporalSource6Report *report){
    if(!history || !current_low || !state || !aligned_low ||
       !aligned_high || !report || !state_valid(state) ||
       current_count<LOW_SLOTS || aligned_count<LOW_SLOTS ||
       high_count<HIGH_SLOTS ||
       history->ready!=1u ||
       history->frames!=state->frames+1u ||
       (state->ready && history->has_previous!=1u))
        return 0;
    for(unsigned i=0u;i<LOW_SLOTS;i++)
        if(!isfinite(current_low[i].re) ||
           !isfinite(current_low[i].im))return 0;

    SautiAacSbrTemporalHfTargets targets;
    if(!sauti_aac_sbr_temporal_hf_targets(history,&targets))
        return 0;

    SautiAacSbrComplex shifted[LOW_SLOTS];
    SautiAacSbrComplex high[HIGH_SLOTS];
    for(unsigned t=0u;t<32u;t++){
        if(t<6u){
            if(state->ready)
                memcpy(shifted+(size_t)t*32u,
                       state->previous_last6+(size_t)t*32u,
                       32u*sizeof(shifted[0]));
            else
                memset(shifted+(size_t)t*32u,0,
                       32u*sizeof(shifted[0]));
        }else
            memcpy(shifted+(size_t)t*32u,
                   current_low+(size_t)(t-6u)*32u,
                   32u*sizeof(shifted[0]));
    }
    for(unsigned t=0u;t<32u;t++){
        const SautiAacSbrTemporalHfSlot *owner=&targets.slot[t];
        const SautiAacSbrPayloadFrame *f=
            owner->source==0u?&history->previous:&history->current;
        if(owner->source>1u || owner->kx!=f->tables.kx ||
           owner->k2!=f->tables.kx+f->tables.m ||
           !sauti_aac_sbr_hf_transpose(
               &f->patches,&f->tables,
               shifted+(size_t)t*32u,32u,1u,
               high+(size_t)t*64u,64u))
            return 0;
    }

    SautiAacSbrTemporalSource6State next=*state;
    memcpy(next.previous_last6,
           current_low+(32u-6u)*32u,
           sizeof(next.previous_last6));
    next.ready=1u;
    next.frames++;
    const SautiAacSbrTemporalSource6Report stats={
        .carried_from_previous=state->ready?6u:0u,
        .current_source_slots=26u,
        .missing_preroll=state->ready?0u:6u,
        .previous_owner_slots=targets.previous_slots,
        .frame_index=next.frames-1u
    };
    memcpy(aligned_low,shifted,sizeof(shifted));
    memcpy(aligned_high,high,sizeof(high));
    *state=next;
    *report=stats;
    return 1;
}
