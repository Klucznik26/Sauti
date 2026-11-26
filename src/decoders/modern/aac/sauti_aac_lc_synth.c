/* Native mono AAC-LC synthesis: dequant -> PNS -> inverse TNS -> IMDCT
 * -> windowing -> overlap-add. Stereo/intensity and other unsupported
 * bitstream tools remain explicitly gated by the frame parser. */
#include "sauti_aac_lc_synth.h"
#include "sauti_aac_imdct.h"
#include "sauti_aac_window.h"
#include "sauti_aac_pns.h"
#include "sauti_aac_layout.h"
#include <math.h>
#include <stdint.h>
#include <string.h>

int sauti_aac_lc_synthesis_init(SautiAacLcSynthesis *state,
                               unsigned previous_shape){
    if(!state || previous_shape>1u)return 0;
    SautiAacLcSynthesis next={0};
    next.previous_shape=previous_shape;
    next.previous_sequence=4u; /* brak poprzedniej ramki: strumien moze zaczac sie dowolna sekwencja okien (np. LONG_START po wycieciu/odczycie od srodka) */
    next.initialized=1u;
    next.pns_seed=0x1f2e3d4cu; /* FFmpeg AAC decoder initial PNS seed */
    *state=next;
    return 1;
}
static int valid_transition(unsigned previous,unsigned current){
    if(previous==4u)return current<=3u;
    if(previous==0u)return current==0u || current==1u;
    if(previous==1u)return current==2u;
    if(previous==2u)return current==2u || current==3u;
    if(previous==3u)return current==0u || current==1u;
    return 0;
}
int sauti_aac_lc_synthesis_spectral(SautiAacLcSynthesis *state,
                                    const SautiAacIcsInfo *ics,
                                    const SautiAacTnsData *tns,
                                    unsigned sample_rate_index,
                                    const float spectral_input[1024],
                                    float pcm[1024]){
    const unsigned layout=SAUTI_AAC_IDX_LAYOUT(sample_rate_index);
    const unsigned n=layout==SAUTI_AAC_LAYOUT_960?960u:1024u,ns=n/8u;      /* long and short transform lengths: 1024 / 128, or 960 / 120 with frameLengthFlag = 1 */
    if(!state || !ics || !spectral_input || !pcm ||
       SAUTI_AAC_IDX_RATE(sample_rate_index)>=13u ||
       (layout!=SAUTI_AAC_LAYOUT_1024 && layout!=SAUTI_AAC_LAYOUT_960) || state->initialized!=1u ||
       state->previous_shape>1u || ics->window_shape>1u ||
       ics->window_sequence>3u)
        return 0;
    for(unsigned i=0;i<n;i++)
        if(!isfinite(state->overlap[i]) ||
           !isfinite(spectral_input[i]))return 0;
    float spectral[1024]={0},time[2048],out[1024],history[1024];
    memcpy(spectral,spectral_input,n*sizeof(float));
    if(tns && !sauti_aac_tns_apply(tns,ics,sample_rate_index,spectral))
        return 0;
    /* SBR-81: a forbidden transition is decoded like FFmpeg does (windowing depends on the current sequence and the saved overlap only)
     * and counted; before, one such frame made the whole stream undecodable although every real decoder plays it. */
    const unsigned transition_ok=valid_transition(state->previous_sequence,ics->window_sequence);
    if(ics->window_sequence==2u){
        for(unsigned block=0;block<8;block++)
            if(!sauti_aac_imdct(spectral+ns*block,ns,
                                time+2u*ns*block))return 0;
    }else if(!sauti_aac_imdct(spectral,n,time))return 0;
    if(!sauti_aac_window_synth_n(n,ics->window_sequence,
                                 state->previous_shape,ics->window_shape,
                                 time,state->overlap,out,history))return 0;
    memcpy(pcm,out,n*sizeof(float));
    memcpy(state->overlap,history,n*sizeof(float));
    state->previous_sequence=ics->window_sequence;
    state->previous_shape=ics->window_shape;
    if(!transition_ok && state->transition_violations<UINT32_MAX)state->transition_violations++;
    return 1;
}
int sauti_aac_lc_synthesis_spectral_ssr(SautiAacLcSynthesis *state,SautiAacSsrState *ssr,const SautiAacIcsInfo *ics,const SautiAacTnsData *tns,
                                        unsigned sample_rate_index,const float spectral_input[1024],const SautiAacSsrGain *gain,float pcm[1024]){
    if(!state || !ssr || !ics || !spectral_input || !pcm || SAUTI_AAC_IDX_RATE(sample_rate_index)>=13u ||
       SAUTI_AAC_IDX_LAYOUT(sample_rate_index)!=SAUTI_AAC_LAYOUT_1024 || state->initialized!=1u ||
       state->previous_shape>1u || ics->window_shape>1u || ics->window_sequence>3u)
        return 0;
    for(unsigned i=0;i<1024;i++)if(!isfinite(spectral_input[i]))return 0;
    float spectral[1024];
    memcpy(spectral,spectral_input,sizeof spectral);
    if(tns && !sauti_aac_tns_apply(tns,ics,sample_rate_index,spectral))return 0;
    const unsigned transition_ok=valid_transition(state->previous_sequence,ics->window_sequence);
    if(!sauti_aac_ssr_synthesis(ssr,ics->window_sequence,state->previous_shape,ics->window_shape,spectral,gain,pcm))return 0;
    state->previous_sequence=ics->window_sequence;
    state->previous_shape=ics->window_shape;
    if(!transition_ok && state->transition_violations<UINT32_MAX)state->transition_violations++;
    return 1;
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
       ics->window_shape>1u || ics->window_sequence>3u)
        return 0;
    for(unsigned j=0;j<1024;j++)
        if(!isfinite(state->overlap[j]))return 0;
    /* The legacy strict entry point still rejects PNS; the newer mono
     * synthesis path accepts PNS but always rejects intensity stereo. */
    if(sections->section_count>SAUTI_AAC_MAX_SECTIONS)return 0;
    for(unsigned s=0;s<sections->section_count;s++){
        unsigned cb=sections->sections[s].codebook;
        if(cb>=14u || (cb==13u && !allow_pns))return 0;
    }
    float spectral[1024];
    if(!sauti_aac_lc_dequantize(quant,sample_rate_index,ics,sections,
                                factors,spectral))return 0;
    uint32_t next_seed=state->pns_seed;
    if(allow_pns && !sauti_aac_pns_apply(ics,sample_rate_index,sections,
                                        factors,&next_seed,spectral))return 0;
    SautiAacLcSynthesis next=*state;
    if(!sauti_aac_lc_synthesis_spectral(&next,ics,tns,
                                        sample_rate_index,spectral,pcm))
        return 0;
    next.pns_seed=next_seed;
    *state=next;
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
