/* SBR-54f: temporal MEAN energy audit of ORIGINAL envelope bins.
 * A single slot can legitimately exceed its envelope mean by 75x;
 * only aggregation across the complete original envelope is useful.
 * Read-only diagnostics; no PCM, normalizing or limiter changes. */
#include "sauti_aac_sbr_temporal_envelope_audit.h"
#include <math.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>

#define SLOT_COUNT 32u
#define BIN_COUNT 64u
#define FRAME_BINS (SLOT_COUNT*BIN_COUNT)
#define NENV SAUTI_AAC_SBR_MAX_ENV
#define LOCAL_GROUPS (2u*NENV)

typedef struct {
    size_t index;
    SautiAacSbrTemporalEnvelopeAuditGroup group;
} Pending;

static int close_number(double a,double b){
    return a==b || fabs(a-b)<=1e-12*fmax(1.0,fmax(fabs(a),fabs(b)));
}

int sauti_aac_sbr_temporal_envelope_audit_push(
    const SautiAacSbrTemporalHfTargets *targets,
    const SautiAacSbrComplex *high,size_t high_count,
    const SautiAacSbrTemporalNoiseFrame *noise,
    const SautiAacSbrTemporalApplyReport *apply,
    const SautiAacSbrTemporalNoiseReport *nr,
    SautiAacSbrTemporalEnvelopeAuditState *state){
    if(!targets || !high || high_count<FRAME_BINS ||
       !noise || !apply || !nr || !state || !state->groups ||
       !state->max_frames ||
       state->max_frames>SIZE_MAX/NENV ||
       state->next_emitted_frame>=state->max_frames ||
       state->next_emitted_frame==UINT64_MAX ||
       !apply->emitted || !nr->emitted ||
       nr->frame_index!=state->next_emitted_frame ||
       apply->frame_index!=nr->frame_index ||
       nr->vectors>FRAME_BINS)return 0;

    Pending pending[LOCAL_GROUPS]={0};
    unsigned used=0u;
    unsigned active=0u;
    for(unsigned t=0u;t<SLOT_COUNT;t++){
        const SautiAacSbrTemporalHfSlot *slot=&targets->slot[t];
        const unsigned source=slot->source,e=slot->envelope;
        if(source>1u || e>=NENV ||
           (source==0u && nr->frame_index==0u) ||
           slot->kx<1u || slot->kx>32u ||
           slot->k2<=slot->kx || slot->k2>64u)return 0;
        const uint64_t original=nr->frame_index-(source==0u?1u:0u);
        if(original>=state->max_frames)return 0;
        const size_t idx=(size_t)original*NENV+e;
        unsigned pos=0u;
        while(pos<used && pending[pos].index!=idx)pos++;
        if(pos==used){
            if(used==LOCAL_GROUPS)return 0;
            pending[pos].index=idx;
            pending[pos].group.used=1u;
            used++;
        }
        SautiAacSbrTemporalEnvelopeAuditGroup *g=&pending[pos].group;
        const SautiAacSbrTemporalEnvelopeAuditGroup *old=
            &state->groups[idx];
        for(unsigned k=0u;k<BIN_COUNT;k++){
            const unsigned i=t*BIN_COUNT+k;
            const unsigned on=k>=slot->kx && k<slot->k2;
            const double e_target=slot->energy[k];
            const double amp=noise->raw_amplitude[i];
            const SautiAacSbrComplex x=high[i];
            if(noise->active[i]!=on ||
               !isfinite(x.re) || !isfinite(x.im) ||
               !isfinite(amp) || amp<0.0 ||
               !isfinite(e_target) || e_target<0.0 ||
               (!on && (e_target!=0.0 || amp!=0.0 ||
                        x.re!=0.0f || x.im!=0.0f)))
                return 0;
            if(!on)continue;
            if(e_target==0.0)return 0;
            const double hf=(double)x.re*x.re+(double)x.im*x.im;
            const double np=amp*amp;
            if(!isfinite(hf)||!isfinite(np) ||
               g->count[k]==UINT32_MAX)return 0;
            if(g->count[k] && !close_number(e_target,g->target_per_slot[k]))
                return 0;
            if(old->count[k] &&
               !close_number(e_target,old->target_per_slot[k]))
                return 0;
            g->target_per_slot[k]=e_target;
            g->count[k]++;
            g->target_total[k]+=e_target;
            g->hf_total[k]+=hf;
            g->noise_total[k]+=np;
            if(!isfinite(g->target_total[k]) ||
               !isfinite(g->hf_total[k]) ||
               !isfinite(g->noise_total[k]))return 0;
            active++;
        }
    }
    if(active!=nr->vectors)return 0;
    /* No mutations until every source slot, owner and aggregate
     * have been validated, including potential overflow on commit. */
    for(unsigned j=0u;j<used;j++){
        const SautiAacSbrTemporalEnvelopeAuditGroup *old=
            &state->groups[pending[j].index],*delta=&pending[j].group;
        if(old->used>1u)return 0;
        for(unsigned k=0u;k<BIN_COUNT;k++){
            if(old->count[k]>UINT32_MAX-delta->count[k] ||
               !isfinite(old->target_total[k]+delta->target_total[k]) ||
               !isfinite(old->hf_total[k]+delta->hf_total[k]) ||
               !isfinite(old->noise_total[k]+delta->noise_total[k]))
                return 0;
        }
    }
    for(unsigned j=0u;j<used;j++){
        SautiAacSbrTemporalEnvelopeAuditGroup *dst=
            &state->groups[pending[j].index];
        const SautiAacSbrTemporalEnvelopeAuditGroup *delta=
            &pending[j].group;
        dst->used=1u;
        for(unsigned k=0u;k<BIN_COUNT;k++){
            if(delta->count[k]){
                if(!dst->count[k])
                    dst->target_per_slot[k]=delta->target_per_slot[k];
                dst->count[k]+=delta->count[k];
                dst->target_total[k]+=delta->target_total[k];
                dst->hf_total[k]+=delta->hf_total[k];
                dst->noise_total[k]+=delta->noise_total[k];
            }
        }
    }
    state->next_emitted_frame++;
    return 1;
}

int sauti_aac_sbr_temporal_envelope_audit_report(
    const SautiAacSbrTemporalEnvelopeAuditState *state,
    SautiAacSbrTemporalEnvelopeAuditReport *out){
    if(!state || !out || !state->groups ||
       !state->max_frames || state->max_frames>SIZE_MAX/NENV ||
       state->next_emitted_frame>state->max_frames)return 0;
    SautiAacSbrTemporalEnvelopeAuditReport stats={0};
    stats.completed_frames=state->next_emitted_frame;
    for(size_t idx=0u;idx<state->max_frames*NENV;idx++){
        const SautiAacSbrTemporalEnvelopeAuditGroup *g=
            &state->groups[idx];
        if(g->used>1u)return 0;
        if(g->used)stats.envelopes++;
        for(unsigned k=0u;k<BIN_COUNT;k++){
            const unsigned count=g->count[k];
            const double e=g->target_total[k],h=g->hf_total[k],
                         n=g->noise_total[k];
            if(!isfinite(e) || !isfinite(h) || !isfinite(n) ||
               e<0.0 || h<0.0 || n<0.0 ||
               (!count && (e!=0.0||h!=0.0||n!=0.0)) ||
               (count && (e<=0.0 ||
                !close_number(e,(double)count*g->target_per_slot[k]))))
                return 0;
            if(!count)continue;
            const double ratio=(h+n)/e;
            if(!isfinite(ratio))return 0;
            stats.active_envelope_bins++;
            stats.total_slot_bins+=count;
            stats.target_total+=e;
            stats.hf_total+=h;
            stats.noise_total+=n;
            if(ratio>1.0001)stats.above_target++;
            if(ratio>2.0)stats.above_twice++;
            if(ratio>10.0)stats.above_tenfold++;
            if(ratio>stats.max_ratio){
                stats.max_ratio=ratio;
                stats.worst_frame=idx/NENV;
                stats.worst_envelope=idx%NENV;
                stats.worst_bin=k;
                stats.worst_slots=count;
            }
        }
    }
    if(!isfinite(stats.target_total) ||
       !isfinite(stats.hf_total) ||
       !isfinite(stats.noise_total))return 0;
    *out=stats;
    return 1;
}
