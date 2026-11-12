#include "sauti_aac_he_ps_delay.h"
#include <math.h>
#include <stdint.h>
#include <string.h>

static int apart(const void *a,size_t an,const void *b,size_t bn){
    const uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x<=y?y-x>=an:x-y>=bn;
}
int sauti_aac_he_ps_delay_push(
    const SautiAacSbrPayloadFrame *ps,
    const SautiAacSbrComplex *qmf,size_t qmf_count,
    SautiAacHePsDelayState *state,
    float *pcm,size_t pcm_count,
    unsigned *produced){
    if(!ps||!qmf||!state||!pcm||!produced||
       ps->has_ps!=1u||qmf_count<32u*64u||pcm_count<4096u||
       state->pending>1u||state->received_frames==UINT64_MAX||
       state->bridge.frames>state->received_frames||
       state->bridge.frames!=state->bridge.render.frames||
       (state->pending?state->bridge.frames+1u!=state->received_frames:
                        state->bridge.frames!=state->received_frames))return 0;
    const size_t qbytes=32u*64u*sizeof(*qmf);
    const size_t pbytes=4096u*sizeof(*pcm);
    if(!apart(ps,sizeof(*ps),qmf,qbytes)||
       !apart(ps,sizeof(*ps),state,sizeof(*state))||
       !apart(qmf,qbytes,state,sizeof(*state))||
       !apart(ps,sizeof(*ps),pcm,pbytes)||
       !apart(qmf,qbytes,pcm,pbytes)||
       !apart(state,sizeof(*state),pcm,pbytes)||
       !apart(produced,sizeof(*produced),state,sizeof(*state))||
       !apart(produced,sizeof(*produced),pcm,pbytes)||
       !apart(produced,sizeof(*produced),ps,sizeof(*ps))||
       !apart(produced,sizeof(*produced),qmf,qbytes))return 0;

    for(size_t i=0;i<32u*64u;i++)
        if(!isfinite(qmf[i].re)||!isfinite(qmf[i].im))return 0;
    SautiAacHePsDelayState next=*state;
    float render[4096u];
    unsigned ready=0u;
    if(next.pending){
        SautiAacSbrComplex span[38u*64u];
        memcpy(span,next.pending_qmf,sizeof(next.pending_qmf));
        memcpy(span+32u*64u,qmf,6u*64u*sizeof(*qmf));
        if(!sauti_aac_he_ps_bridge_render(&next.pending_ps,span,38u*64u,
                                           &next.bridge,render,4096u))
            return 0;
        ready=1u;
    }
    next.pending_ps=*ps;
    memcpy(next.pending_qmf,qmf,qbytes);
    next.pending=1u;
    next.received_frames++;
    if(ready)memcpy(pcm,render,sizeof(render));
    *state=next;
    *produced=ready;
    return 1;
}
