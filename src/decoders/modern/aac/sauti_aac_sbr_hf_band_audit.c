/* SBR-54t: compare per-band ORIGINAL 53b HF and shifted-source
 * SBR-54s HF. Does not compare to FFmpeg or modify playback/PCM. */
#include "sauti_aac_sbr_hf_band_audit.h"
#include <math.h>

#define HF_COUNT (32u*64u)

int sauti_aac_sbr_hf_band_audit_add(
    const SautiAacSbrComplex *old_hf,size_t old_count,
    const SautiAacSbrComplex *new_hf,size_t new_count,
    const SautiAacSbrTemporalSource6Report *source6,
    SautiAacSbrHfBandAuditState *state){
    if(!old_hf || !new_hf || !source6 || !state ||
       old_count<HF_COUNT || new_count<HF_COUNT ||
       source6->frame_index!=state->frames ||
       source6->current_source_slots!=26u ||
       source6->previous_owner_slots>6u ||
       source6->carried_from_previous!=(state->frames?6u:0u) ||
       source6->missing_preroll!=(state->frames?0u:6u) ||
       state->frames==UINT64_MAX)
        return 0;
    for(unsigned k=0u;k<64u;k++)
        if(!isfinite(state->old_energy[k]) ||
           !isfinite(state->new_energy[k]) ||
           !isfinite(state->cross[k]) ||
           !isfinite(state->difference[k]) ||
           state->changed[k]>state->active[k])return 0;
    if(!isfinite(state->peak_difference) ||
       state->peak_difference<0.0 ||
       state->changed_total>state->active_total)return 0;

    /* Check all 2048 cells including ignored first-frame preroll,
     * so corruption in an unused slot cannot be silently accepted. */
    for(unsigned i=0u;i<HF_COUNT;i++){
        const SautiAacSbrComplex a=old_hf[i],b=new_hf[i];
        if(!isfinite(a.re) || !isfinite(a.im) ||
           !isfinite(b.re) || !isfinite(b.im))return 0;
    }

    SautiAacSbrHfBandAuditState next=*state;
    for(unsigned t=source6->missing_preroll;t<32u;t++)
        for(unsigned k=0u;k<64u;k++){
            const unsigned i=t*64u+k;
            const SautiAacSbrComplex a=old_hf[i],b=new_hf[i];
            if(a.re==0.0f && a.im==0.0f &&
               b.re==0.0f && b.im==0.0f)continue;
            const double ar=a.re,ai=a.im,br=b.re,bi=b.im;
            const double dr=ar-br,di=ai-bi;
            const double e_old=ar*ar+ai*ai;
            const double e_new=br*br+bi*bi;
            const double cross=ar*br+ai*bi;
            const double diff=dr*dr+di*di;
            if(!isfinite(e_old) || !isfinite(e_new) ||
               !isfinite(cross) || !isfinite(diff) ||
               next.active[k]==UINT64_MAX ||
               next.active_total==UINT64_MAX)return 0;
            next.old_energy[k]+=e_old;
            next.new_energy[k]+=e_new;
            next.cross[k]+=cross;
            next.difference[k]+=diff;
            next.active[k]++;
            next.active_total++;
            if(a.re!=b.re || a.im!=b.im){
                if(next.changed[k]==UINT64_MAX ||
                   next.changed_total==UINT64_MAX)return 0;
                next.changed[k]++;
                next.changed_total++;
            }
            const double amplitude=sqrt(diff);
            if(amplitude>next.peak_difference){
                next.peak_difference=amplitude;
                next.peak_frame=state->frames;
                next.peak_slot=(uint8_t)t;
                next.peak_band=(uint8_t)k;
            }
        }
    for(unsigned k=0u;k<64u;k++)
        if(!isfinite(next.old_energy[k]) ||
           !isfinite(next.new_energy[k]) ||
           !isfinite(next.cross[k]) ||
           !isfinite(next.difference[k]))return 0;
    if(!isfinite(next.peak_difference))return 0;
    next.frames++;
    *state=next;
    return 1;
}
