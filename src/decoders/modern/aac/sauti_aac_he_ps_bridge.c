#include "sauti_aac_he_ps_bridge.h"
#include <stdint.h>

static int apart(const void *a,size_t n,const void *b,size_t m){
    const uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x<=y?y-x>=n:x-y>=m;
}
#include <string.h>

int sauti_aac_he_ps_bridge_render(
    const SautiAacSbrPayloadFrame *payload,
    const SautiAacSbrComplex *mono_qmf,size_t qmf_count,
    SautiAacHePsBridgeState *state,
    float *stereo_pcm,size_t stereo_pcm_count){
    if(!payload||!mono_qmf||!state||!stereo_pcm||
       payload->has_ps!=1u||qmf_count<38u*64u||
       stereo_pcm_count<4096u||state->frames==UINT64_MAX||
       state->frames!=state->render.frames||
       !apart(stereo_pcm,4096u*sizeof(*stereo_pcm),state,sizeof(*state))||
       !apart(stereo_pcm,4096u*sizeof(*stereo_pcm),payload,sizeof(*payload))||
       !apart(stereo_pcm,4096u*sizeof(*stereo_pcm),mono_qmf,38u*64u*sizeof(*mono_qmf))||
       !apart(state,sizeof(*state),payload,sizeof(*payload))||
       !apart(state,sizeof(*state),mono_qmf,38u*64u*sizeof(*mono_qmf)))
        return 0;

    SautiAacHePsBridgeState next=*state;
    SautiAacPsRenderPlan plan;
    float stage[4096u];
    if(!sauti_aac_ps_render_plan_build(&payload->ps,&next.render,&plan)||
       !sauti_aac_ps_pcm_stereo_from_mono_qmf(
           &plan.schedule,&plan.complex_matrix,
           mono_qmf,qmf_count,&next.pcm,stage,4096u))
        return 0;

    next.frames++;
    memcpy(stereo_pcm,stage,sizeof(stage));
    *state=next;
    return 1;
}
