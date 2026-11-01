/* AAC-12 isolated LC mono synthesis pipeline.
 * TNS/PNS/intensity and full raw_data_block element parsing remain absent;
 * callers must not bypass the unsupported-tools gate. */
#include "sauti_aac_lc_synth.h"
#include "sauti_aac_imdct.h"
#include "sauti_aac_window.h"
#include "sauti_aac_pns.h"
#include <math.h>
#include <string.h>

int sauti_aac_lc_synthesis_init(SautiAacLcSynthesis *state,
                               unsigned previous_shape){
    if(!state || previous_shape>1u)return 0;
    SautiAacLcSynthesis next={0};
    next.previous_shape=previous_shape;
    next.previous_sequence=0u;
    next.initialized=1u;
    next.pns_seed=0x12345678u;
    *state=next;
    return 1;
}
static int valid_transition(unsigned previous,unsigned current){
    if(previous==0u)return current==0u || current==1u;
    if(previous==1u)return current==2u;
    if(previous==2u)return current==2u || current==3u;
    if(previous==3u)return current==0u || current==1u;
    return 0;
}
static int synth_internal(int allow_pns,const SautiAacTnsData *tns,
                                SautiAacLcSynthesis *state,
                                unsigned sample_rate_index,
                                const SautiAacIcsInfo *ics,
                                const SautiAacSections *sections,
                                const SautiAacScaleFactors *factors,
                                const int32_t quant[1024],
                                float pcm[1024]){
    if(!state || !ics || !sections || !factors || !quant || !pcm ||
       state->initialized!=1u || state->previous_shape>1u ||
       ics->window_shape>1u || ics->window_sequence>3u ||
       !valid_transition(state->previous_sequence,ics->window_sequence))
        return 0;
    for(unsigned j=0;j<1024;j++)
        if(!isfinite(state->overlap[j]))return 0;
    /* Current synthesis pipeline cannot synthesize PNS or intensity
     * data. Refuse these bands explicitly, including valid bitstreams. */
    if(sections->section_count>SAUTI_AAC_MAX_SECTIONS)return 0;
    for(unsigned s=0;s<sections->section_count;s++){
        unsigned cb=sections->sections[s].codebook;
        if(cb>=14u || (cb==13u && !allow_pns))return 0;
    }
    float spectral[1024],time[2048],out[1024],next_history[1024];
    if(!sauti_aac_lc_dequantize(quant,sample_rate_index,ics,sections,
                                factors,spectral))return 0;
    uint32_t next_seed=state->pns_seed;
    if(allow_pns && !sauti_aac_pns_apply(ics,sample_rate_index,sections,
                                        factors,&next_seed,spectral))return 0;
    if(tns && !sauti_aac_tns_apply(tns,ics,sample_rate_index,spectral))return 0;
    if(ics->window_sequence==2u){
        for(unsigned block=0;block<8;block++)
            if(!sauti_aac_imdct(spectral+128u*block,128u,
                                time+256u*block))return 0;
    }else{
        if(!sauti_aac_imdct(spectral,1024u,time))return 0;
    }
    if(!sauti_aac_window_synth(ics->window_sequence,state->previous_shape,
                               ics->window_shape,time,state->overlap,
                               out,next_history))return 0;
    memcpy(pcm,out,sizeof(out));
    memcpy(state->overlap,next_history,sizeof(next_history));
    state->previous_sequence=ics->window_sequence;
    state->previous_shape=ics->window_shape;
    state->pns_seed=next_seed;
    return 1;
}
int sauti_aac_lc_synthesis_frame_pns(SautiAacLcSynthesis *state,
       unsigned rate,const SautiAacIcsInfo *ics,
       const SautiAacSections *sections,const SautiAacScaleFactors *factors,
       const int32_t quant[1024],float pcm[1024]){
    return synth_internal(1,NULL,state,rate,ics,sections,factors,quant,pcm);
}
int sauti_aac_lc_synthesis_frame(SautiAacLcSynthesis *state,
       unsigned rate,const SautiAacIcsInfo *ics,
       const SautiAacSections *sections,const SautiAacScaleFactors *factors,
       const int32_t quant[1024],float pcm[1024]){
    return synth_internal(0,NULL,state,rate,ics,sections,factors,quant,pcm);
}

int sauti_aac_lc_synthesis_frame_tns(SautiAacLcSynthesis *state,
       unsigned rate,const SautiAacIcsInfo *ics,
       const SautiAacSections *sections,const SautiAacScaleFactors *factors,
       const int32_t quant[1024],const SautiAacTnsData *tns,
       float pcm[1024]){
    return synth_internal(1,tns,state,rate,ics,sections,factors,quant,pcm);
}
