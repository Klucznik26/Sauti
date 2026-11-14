#include "sauti_aac_ps_pcm_stereo.h"
#include <math.h>
#include <stdint.h>
#include <string.h>

#define PS_PCM_SLOTS 32u
#define PS_PCM_BANDS 64u
#define PS_PCM_SAMPLES (PS_PCM_SLOTS*PS_PCM_BANDS)
#define PS_PCM_INTERLEAVED (PS_PCM_SAMPLES*2u)

static int apart(const void *a,size_t an,const void *b,size_t bn){
    const uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x<=y?y-x>=an:x-y>=bn;
}
static int render_pair(const SautiAacSbrComplex *left_qmf,
                       const SautiAacSbrComplex *right_qmf,
                       SautiAacPsPcmStereoState *next,float *pcm){
    float l[PS_PCM_SAMPLES],r[PS_PCM_SAMPLES];
    if(!sauti_aac_sbr_qmf_synth_64(left_qmf,PS_PCM_SAMPLES,PS_PCM_SLOTS,
                l,PS_PCM_SAMPLES,&next->left)||
       !sauti_aac_sbr_qmf_synth_64(right_qmf,PS_PCM_SAMPLES,PS_PCM_SLOTS,
                r,PS_PCM_SAMPLES,&next->right))return 0;
    for(size_t i=0;i<PS_PCM_SAMPLES;i++){
        if(!isfinite(l[i])||!isfinite(r[i]))return 0;
        pcm[2u*i]=l[i];pcm[2u*i+1u]=r[i];
    }
    return 1;
}
int sauti_aac_ps_pcm_stereo_from_qmf(
    const SautiAacSbrComplex *left_qmf,size_t left_count,
    const SautiAacSbrComplex *right_qmf,size_t right_count,
    SautiAacPsPcmStereoState *state,
    float *pcm_interleaved,size_t pcm_count){
    if(!left_qmf||!right_qmf||!state||!pcm_interleaved||
       left_count<PS_PCM_SAMPLES||right_count<PS_PCM_SAMPLES||
       pcm_count<PS_PCM_INTERLEAVED)return 0;
    const size_t qbytes=PS_PCM_SAMPLES*sizeof(*left_qmf);
    const size_t pbytes=PS_PCM_INTERLEAVED*sizeof(*pcm_interleaved);
    if(!apart(left_qmf,qbytes,right_qmf,qbytes)||
       !apart(left_qmf,qbytes,state,sizeof(*state))||
       !apart(right_qmf,qbytes,state,sizeof(*state))||
       !apart(left_qmf,qbytes,pcm_interleaved,pbytes)||
       !apart(right_qmf,qbytes,pcm_interleaved,pbytes)||
       !apart(state,sizeof(*state),pcm_interleaved,pbytes))return 0;
    SautiAacPsPcmStereoState next=*state;
    float tmp[PS_PCM_INTERLEAVED];
    if(!render_pair(left_qmf,right_qmf,&next,tmp))return 0;
    memcpy(pcm_interleaved,tmp,pbytes);
    *state=next;
    return 1;
}
int sauti_aac_ps_pcm_stereo_from_mono_qmf(
    const SautiAacPsHybridSchedule *schedule,
    const SautiAacPsComplexMatrix *matrix,
    const SautiAacSbrComplex *mono_qmf,size_t mono_count,
    SautiAacPsPcmPipelineState *state,
    float *pcm_interleaved,size_t pcm_count){
    if(!schedule||!matrix||!mono_qmf||!state||!pcm_interleaved||
       mono_count<38u*64u||pcm_count<PS_PCM_INTERLEAVED)return 0;
    const unsigned bands=schedule->hybrid_bands;
    if(!((schedule->parameter_bands==20u&&bands==71u)||
         (schedule->parameter_bands==34u&&bands==91u)))return 0;
    const size_t pbytes=PS_PCM_INTERLEAVED*sizeof(*pcm_interleaved);
    const size_t inbytes=38u*64u*sizeof(*mono_qmf);
    if(!apart(mono_qmf,inbytes,state,sizeof(*state))||
       !apart(mono_qmf,inbytes,pcm_interleaved,pbytes)||
       !apart(state,sizeof(*state),pcm_interleaved,pbytes)||
       !apart(schedule,sizeof(*schedule),pcm_interleaved,pbytes)||
       !apart(matrix,sizeof(*matrix),pcm_interleaved,pbytes)||
       !apart(schedule,sizeof(*schedule),state,sizeof(*state))||
       !apart(matrix,sizeof(*matrix),state,sizeof(*state)))return 0;
    SautiAacPsPcmPipelineState next=*state;
    SautiAacSbrComplex hl[32u*91u],hr[32u*91u];
    SautiAacSbrComplex ql[PS_PCM_SAMPLES],qr[PS_PCM_SAMPLES];
    float pcm[PS_PCM_INTERLEAVED];
    if(!sauti_aac_ps_stereo_hybrid_apply(schedule,matrix,mono_qmf,mono_count,
                                         &next.hybrid,hl,(size_t)32u*bands,
                                         hr,(size_t)32u*bands)||
       !sauti_aac_ps_hybrid_synthesize_stereo(schedule,hl,(size_t)32u*bands,
                                              hr,(size_t)32u*bands,
                                              ql,PS_PCM_SAMPLES,qr,PS_PCM_SAMPLES)||
       !render_pair(ql,qr,&next.synthesis,pcm))return 0;
    memcpy(pcm_interleaved,pcm,pbytes);
    *state=next;
    return 1;
}
