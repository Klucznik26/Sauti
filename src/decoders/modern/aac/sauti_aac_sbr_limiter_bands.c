/* HE-AAC SBR-17: derive f_tablelim from low-resolution boundaries
 * and internal HF patch junctions. Constant-threshold pruning follows
 * the classic 0.49/bands-per-octave spacing rule. Unlike an unchecked
 * in-place merging loop, this implementation protects both outer edges
 * and leaves the caller's output untouched on every invalid input.
 *
 * Only builds frequency indices. Actual bounded gain adjustment remains
 * in SBR-16. No heap or external codec dependency. */
#include "sauti_aac_sbr_limiter_bands.h"
#include "sauti_aac_sbr_hf.h"
#include <string.h>

static int important(unsigned edge,
                     unsigned kx, unsigned k2,
                     const SautiAacSbrPatches *patches) {
    if(edge==kx || edge==k2)return 1;
    for(unsigned i=1u;i<patches->count;i++)
        if(edge==patches->target[i])return 1;
    return 0;
}

int sauti_aac_sbr_limiter_bands_build(
    const SautiAacSbrTables *tables,
    const SautiAacSbrPatches *patches,
    unsigned bs_limiter_bands,
    SautiAacSbrLimiterBands *out) {
    if(!out || bs_limiter_bands>3u ||
       !sauti_aac_sbr_hf_map_valid(patches,tables))
        return 0;

    const unsigned kx=tables->kx,k2=tables->kx+tables->m;
    SautiAacSbrLimiterBands next;
    memset(&next,0,sizeof(next));
    if(bs_limiter_bands==0u){
        next.count=1u;next.edges[0]=(uint8_t)kx;
        next.edges[1]=(uint8_t)k2;
        *out=next;return 1;
    }

    /* At most 25 low edges and 5 extra internal patch boundaries.
     * An insertion sort keeps all entries in a tiny fixed stack array.
     * Duplicates are explicitly filtered in the merging pass. */
    uint8_t candidates[
        SAUTI_AAC_SBR_MAX_MASTER_BANDS/2u+
        SAUTI_AAC_SBR_MAX_PATCHES+2u];
    unsigned n=0u;
    for(unsigned i=0u;i<=tables->n_low;i++)
        candidates[n++]=tables->low[i];
    for(unsigned i=1u;i<patches->count;i++){
        const unsigned edge=patches->target[i];
        if(edge<=kx || edge>=k2)return 0;
        candidates[n++]=(uint8_t)edge;
    }
    for(unsigned i=1u;i<n;i++){
        const uint8_t value=candidates[i];
        unsigned j=i;
        while(j && candidates[j-1u]>value){
            candidates[j]=candidates[j-1u];
            --j;
        }
        candidates[j]=value;
    }
    if(n<2u || candidates[0]!=kx ||
       candidates[n-1u]!=k2)return 0;

    static const double threshold[3]={
        1.32715174233856803909,
        1.18509277094158210129,
        1.11987160404675912501
    };
    const double ratio=threshold[bs_limiter_bands-1u];
    unsigned size=1u;
    next.edges[0]=(uint8_t)kx;
    for(unsigned i=1u;i<n;i++){
        const unsigned edge=candidates[i];
        if(edge>k2 || edge<kx)return 0;
        if(edge==next.edges[size-1u])continue; /* duplicate */
        int retain=1;
        while(size>0u){
            const unsigned previous=next.edges[size-1u];
            if((double)edge >= (double)previous*ratio)
                break; /* candidate sufficiently distant */
            const int keep_new=important(edge,kx,k2,patches);
            const int keep_old=important(previous,kx,k2,patches);
            if(keep_new && !keep_old){
                --size; /* replace weaker close boundary, re-evaluate */
                continue;
            }
            if(!keep_new)retain=0;
            /* Two important boundaries must both survive. */
            break;
        }
        if(!retain)continue;
        if(size>=SAUTI_AAC_SBR_MAX_LIMITER_BANDS+1u)
            return 0;
        next.edges[size++]=(uint8_t)edge;
    }
    if(size<2u || size>SAUTI_AAC_SBR_MAX_LIMITER_BANDS+1u ||
       next.edges[0]!=kx || next.edges[size-1u]!=k2)
        return 0;
    for(unsigned i=1u;i<size;i++)
        if(next.edges[i]<=next.edges[i-1u])return 0;
    /* Every internal patch junction must survive threshold pruning. */
    for(unsigned i=1u;i<patches->count;i++){
        int found=0;
        for(unsigned j=1u;j+1u<size;j++)
            if(next.edges[j]==patches->target[i])found=1;
        if(!found)return 0;
    }
    next.count=size-1u;
    *out=next;
    return 1;
}

int sauti_aac_sbr_hf_limit_auto(
    const SautiAacSbrTables *tables,
    const SautiAacSbrPatches *patches,
    unsigned bs_limiter_bands,
    const SautiAacSbrEnergyFrame *energy,
    const uint8_t *slot_borders,
    unsigned slots,
    unsigned gain_mode,
    const SautiAacSbrComplex *input,
    size_t input_count,
    SautiAacSbrComplex *output,
    size_t output_count,
    SautiAacSbrLimiterReport *report) {
    SautiAacSbrLimiterBands freq;
    if(!sauti_aac_sbr_limiter_bands_build(
            tables,patches,bs_limiter_bands,&freq))return 0;
    return sauti_aac_sbr_hf_limit(
        tables,energy,slot_borders,slots,freq.edges,freq.count,
        gain_mode,input,input_count,output,output_count,report);
}
