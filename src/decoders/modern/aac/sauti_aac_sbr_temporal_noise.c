/* HE-AAC SBR-53f: per-slot SBR noise ROM reference sequence,
 * aligned with the ONE-FRAME-DELAYED HF QMF from SBR-53e.
 * Deliberately does not mix into audio or publish PCM. */
#include "sauti_aac_sbr_temporal_noise.h"
#include <math.h>
#include <string.h>

static int temporal_noise_state_valid(
    const SautiAacSbrTemporalNoiseState *state){
    if(!state || state->ready>1u || state->closed>1u ||
       (!state->ready &&
        (state->closed || state->frames || state->emitted_frames)) ||
       (state->ready && !state->frames) ||
       (state->closed ?
        state->emitted_frames!=state->frames :
        (state->ready && state->emitted_frames+1u!=state->frames)))
        return 0;
    return 1;
}

static int produce(const SautiAacSbrTemporalHfTargets *targets,
                   SautiAacSbrNoiseRomState *rom,
                   SautiAacSbrTemporalNoiseFrame *output,
                   SautiAacSbrTemporalNoiseReport *report){
    SautiAacSbrTemporalNoiseFrame staged={0};
    unsigned vectors=0u,nonzero=0u;
    for(unsigned t=0u;t<32u;t++){
        const SautiAacSbrTemporalHfSlot *slot=&targets->slot[t];
        const unsigned kx=slot->kx,k2=slot->k2;
        if(slot->source>1u || kx<1u || kx>32u ||
           k2<=kx || k2>64u)return 0;
        for(unsigned k=kx;k<k2;k++){
            const double e=slot->energy[k];
            const double q=slot->noise_factor[k];
            if(!isfinite(e) || !isfinite(q) ||
               e<0.0 || q<0.0 || slot->harmonic[k]>1u)
                return 0;
            /* SBR-53f measures raw noise amplitude only. A later
             * sinusoid gate and 5-tap smoothing will refine it. */
            const double amplitude=sqrt(e)*sqrt(q/(1.0+q));
            if(!isfinite(amplitude))return 0;
            SautiAacSbrComplex vector;
            if(!sauti_aac_sbr_noise_rom_next(rom,&vector))return 0;
            const size_t idx=(size_t)t*64u+k;
            staged.vector[idx]=vector;
            staged.raw_amplitude[idx]=amplitude;
            vectors++;
            if(amplitude>0.0)nonzero++;
        }
    }
    report->vectors=vectors;
    report->nonzero_amplitudes=nonzero;
    report->rom_index=rom->index;
    *output=staged;
    return 1;
}

static unsigned expected_pending(
    const SautiAacSbrTemporalHistory *history,
    const SautiAacSbrTemporalHfTargets *targets){
    const SautiAacSbrPayloadFrame *f=&history->current;
    const unsigned last=f->grid.env_count;
    if(!last || f->env_edges[last]<=32u)return 0u;
    unsigned count=0u;
    for(unsigned t=0u;t<32u;t++)
        if(targets->slot[t].source==1u &&
           targets->slot[t].envelope==last-1u)
            count++;
    return count;
}

int sauti_aac_sbr_temporal_noise_prepare(
    const SautiAacSbrTemporalHistory *history,
    const SautiAacSbrTemporalApplyReport *apply_report,
    SautiAacSbrTemporalNoiseState *state,
    SautiAacSbrTemporalNoiseFrame *output,
    SautiAacSbrTemporalNoiseReport *report){
    if(!history || !apply_report || !state || !output || !report ||
       !temporal_noise_state_valid(state) || state->closed ||
       !history->frames || state->frames==UINT64_MAX ||
       state->frames+1u!=history->frames ||
       apply_report->emitted!=(unsigned)state->ready ||
       (state->ready &&
        apply_report->frame_index!=state->frames-1u))
        return 0;

    SautiAacSbrTemporalHfTargets targets;
    if(!sauti_aac_sbr_temporal_hf_targets(history,&targets) ||
       apply_report->pending_slots!=expected_pending(history,&targets))
        return 0;

    SautiAacSbrTemporalNoiseState next=*state;
    SautiAacSbrTemporalNoiseReport stats={0};
    SautiAacSbrTemporalNoiseFrame staged;
    if(state->ready){
        if(!produce(&next.held_targets,&next.rom,&staged,&stats))
            return 0;
        stats.emitted=1u;
        stats.frame_index=state->frames-1u;
        next.emitted_frames++;
    }
    next.held_targets=targets;
    next.frames=history->frames;
    next.ready=1u;

    if(stats.emitted)*output=staged;
    *state=next;
    *report=stats;
    return 1;
}

int sauti_aac_sbr_temporal_noise_flush(
    const SautiAacSbrTemporalApplyReport *apply_report,
    SautiAacSbrTemporalNoiseState *state,
    SautiAacSbrTemporalNoiseFrame *output,
    SautiAacSbrTemporalNoiseReport *report){
    if(!apply_report || !state || !output || !report ||
       !temporal_noise_state_valid(state) ||
       !state->ready || state->closed ||
       apply_report->emitted!=1u ||
       apply_report->pending_slots!=0u ||
       apply_report->frame_index!=state->frames-1u)
        return 0;

    SautiAacSbrTemporalNoiseState next=*state;
    SautiAacSbrTemporalNoiseFrame staged;
    SautiAacSbrTemporalNoiseReport stats={0};
    if(!produce(&next.held_targets,&next.rom,&staged,&stats))
        return 0;
    stats.emitted=1u;
    stats.frame_index=state->frames-1u;
    next.emitted_frames++;
    next.closed=1u;
    *output=staged;
    *state=next;
    *report=stats;
    return 1;
}
