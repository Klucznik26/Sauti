/* SBR-54u: independent side-data provenance of four strongest
 * HF discrepancy destination bands. No effect on audio state. */
#include "sauti_aac_sbr_hf_patch_route_audit.h"
#include "sauti_aac_sbr_hf.h"
#include <stdint.h>

static int check_state(const SautiAacSbrHfPatchRouteAuditState *s){
    if(!s || s->frames>=UINT64_MAX/32u)return 0;
    for(unsigned b=0;b<4u;b++){
        if(s->mapped[b]>s->frames*32u ||
           s->outside[b]>s->frames*32u ||
           s->unmapped[b]>s->frames*32u ||
           s->previous_owner[b]>s->frames*32u)
            return 0;
        /* Counts for all destination slots partition the frames. */
        if(s->mapped[b]>UINT64_MAX-s->outside[b] ||
           s->mapped[b]+s->outside[b]>UINT64_MAX-s->unmapped[b] ||
           s->mapped[b]+s->outside[b]+s->unmapped[b]!=s->frames*32u)
            return 0;
        uint64_t total=0u;
        for(unsigned k=0;k<32u;k++){
            if(s->sources[b][k]>s->mapped[b] ||
               total>UINT64_MAX-s->sources[b][k])return 0;
            total+=s->sources[b][k];
        }
        if(total!=s->mapped[b])return 0;
    }
    return 1;
}

int sauti_aac_sbr_hf_patch_route_audit_add(
    const SautiAacSbrTemporalHistory *history,
    SautiAacSbrHfPatchRouteAuditState *state){
    if(!history || !check_state(state) ||
       history->ready!=1u ||
       history->frames!=state->frames+1u ||
       (state->frames && history->has_previous!=1u))
        return 0;
    SautiAacSbrTemporalHfTargets owners;
    if(!sauti_aac_sbr_temporal_hf_targets(history,&owners))
        return 0;
    SautiAacSbrHfPatchRouteAuditState next=*state;
    for(unsigned slot=0u;slot<32u;slot++){
        const SautiAacSbrTemporalHfSlot *o=&owners.slot[slot];
        if(o->source>1u)return 0;
        const SautiAacSbrPayloadFrame *f=
            o->source==0u?&history->previous:&history->current;
        if(o->kx!=f->tables.kx ||
           o->k2!=(unsigned)f->tables.kx+f->tables.m ||
           !sauti_aac_sbr_hf_map_valid(&f->patches,&f->tables))
            return 0;
        for(unsigned b=0u;b<4u;b++){
            const unsigned k=SAUTI_AAC_SBR_ROUTE_BAND_FIRST+b;
            if(o->source==0u)next.previous_owner[b]++;
            if(k<o->kx || k>=o->k2){
                next.outside[b]++;
                continue;
            }
            const int source=f->patches.source_for_target[k];
            if(source==-1){
                next.unmapped[b]++;
                continue;
            }
            if(source<0 || source>=32 ||
               (unsigned)source>=o->kx)return 0;
            next.sources[b][(unsigned)source]++;
            next.mapped[b]++;
        }
    }
    next.frames++;
    if(!check_state(&next))return 0;
    *state=next;
    return 1;
}
