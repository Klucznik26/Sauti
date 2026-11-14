/* HE-AAC SBR-53b: continuous real QMF analysis with owner-specific
 * high-band baseline transposition. This stage never publishes PCM.
 * The existing SBR-31 playback path remains unchanged. */
#include "sauti_aac_sbr_temporal_qmf.h"
#include <string.h>

int sauti_aac_sbr_temporal_qmf_prepare(
    const SautiAacSbrTemporalHistory *history,
    const float *core_pcm,size_t core_count,
    SautiAacSbrQmfAnalysisState *analysis_state,
    SautiAacSbrComplex *low_qmf,size_t low_count,
    SautiAacSbrComplex *baseline_hf,size_t high_count){
    if(!history || !core_pcm || !analysis_state ||
       !low_qmf || !baseline_hf ||
       core_count<1024u || low_count<32u*32u ||
       high_count<32u*64u)
        return 0;

    /* SBR-53a validates every decoded energy/noise target and
     * SBR-52 resolves every slot without gaps or overlaps. */
    SautiAacSbrTemporalHfTargets targets;
    if(!sauti_aac_sbr_temporal_hf_targets(history,&targets))
        return 0;

    SautiAacSbrQmfAnalysisState next=*analysis_state;
    SautiAacSbrComplex low[32u*32u];
    SautiAacSbrComplex high[32u*64u];
    if(!sauti_aac_sbr_qmf_analyze_32(
           core_pcm,core_count,32u,32u,low,32u*32u,&next))
        return 0;

    for(unsigned slot=0u;slot<32u;slot++){
        const SautiAacSbrTemporalHfSlot *owner=&targets.slot[slot];
        const SautiAacSbrPayloadFrame *frame=
            owner->source==0u?&history->previous:&history->current;
        if(owner->source>1u ||
           owner->kx!=frame->tables.kx ||
           owner->k2!=frame->tables.kx+frame->tables.m ||
           !sauti_aac_sbr_hf_transpose(
               &frame->patches,&frame->tables,
               low+(size_t)slot*32u,32u,1u,
               high+(size_t)slot*64u,64u))
            return 0;
    }

    memcpy(low_qmf,low,sizeof(low));
    memcpy(baseline_hf,high,sizeof(high));
    *analysis_state=next;
    return 1;
}
