#include "sauti_aac_he_ps_delay.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define Q (32u*64u)
#define PCM (4096u)
static SautiAacSbrPayloadFrame fixture(unsigned mode){
    SautiAacSbrPayloadFrame p={0};
    p.has_ps=1u;p.ps.resolved_num_env=1u;p.ps.border_position[0]=31u;
    p.ps.iid_count=mode;p.ps.icc_count=mode;
    p.ps.header.enable_iid=1u;p.ps.header.enable_icc=1u;
    p.ps.iid[0][0]=2;p.ps.icc[0][0]=3;
    return p;
}
static void fill(SautiAacSbrComplex *q,unsigned f){
    for(unsigned t=0;t<32u;t++)for(unsigned k=0;k<64u;k++){
        const int v=(int)((f*29u+t*17u+k*13u)%107u)-53;
        q[(size_t)t*64u+k]=(SautiAacSbrComplex){v*0.004f,v*-0.002f};
    }
}
int main(void){
    SautiAacHePsDelayState state={0};
    SautiAacHePsBridgeState reference={0};
    SautiAacSbrPayloadFrame payload;
    SautiAacSbrComplex prev[Q],now[Q],span[38u*64u];
    float pcm[PCM+1u],expected[PCM];
    unsigned produced=99u;
    for(unsigned mode=20u;mode<=34u;mode+=14u){
        memset(&state,0,sizeof(state));memset(&reference,0,sizeof(reference));
        payload=fixture(mode);
        fill(prev,0u);
        for(unsigned i=0;i<PCM+1u;i++)pcm[i]=123.0f;
        produced=99u;
        assert(sauti_aac_he_ps_delay_push(&payload,prev,Q,&state,
                                           pcm,PCM,&produced));
        assert(produced==0u&&state.pending==1u&&state.received_frames==1u);
        assert(state.bridge.frames==0u);
        for(unsigned i=0;i<PCM+1u;i++)assert(pcm[i]==123.0f);
        for(unsigned f=1;f<=3u;f++){
            fill(now,f);
            memcpy(span,prev,sizeof(prev));
            memcpy(span+Q,now,6u*64u*sizeof(*now));
            assert(sauti_aac_he_ps_bridge_render(&payload,span,38u*64u,
                                                  &reference,expected,PCM));
            produced=99u;pcm[PCM]=777.0f;
            assert(sauti_aac_he_ps_delay_push(&payload,now,Q,&state,
                                               pcm,PCM,&produced));
            assert(produced==1u);
            assert(memcmp(pcm,expected,sizeof(expected))==0);
            assert(pcm[PCM]==777.0f);
            assert(memcmp(&state.bridge,&reference,sizeof(reference))==0);
            assert(state.received_frames==(uint64_t)f+1u);
            memcpy(prev,now,sizeof(prev));
        }
        /* A valid pending frame is not flushed without real lookahead. */
        assert(state.received_frames==state.bridge.frames+1u);
        const SautiAacHePsDelayState old=state;
        float saved[PCM+1u];
        memcpy(saved,pcm,sizeof(saved));
        produced=71u;
        now[Q-1u].re=NAN;
        assert(!sauti_aac_he_ps_delay_push(&payload,now,Q,&state,
                                            pcm,PCM,&produced));
        assert(produced==71u);
        assert(memcmp(&old,&state,sizeof(old))==0);
        assert(memcmp(saved,pcm,sizeof(saved))==0);
        now[Q-1u].re=0.0f;
        assert(!sauti_aac_he_ps_delay_push(&payload,now,Q-1u,&state,
                                            pcm,PCM,&produced));
        assert(!sauti_aac_he_ps_delay_push(&payload,now,Q,&state,
                                            pcm,PCM-1u,&produced));
        payload.has_ps=0u;
        assert(!sauti_aac_he_ps_delay_push(&payload,now,Q,&state,
                                            pcm,PCM,&produced));
        payload.has_ps=1u;
        assert(memcmp(&old,&state,sizeof(old))==0);
        assert(memcmp(saved,pcm,sizeof(saved))==0);
        assert(produced==71u);
        /* Pending PS is corrupted; rejection must not advance history. */
        state.pending_ps.ps.iid_count=1u;
        const SautiAacHePsDelayState bad=state;
        assert(!sauti_aac_he_ps_delay_push(&payload,now,Q,&state,
                                            pcm,PCM,&produced));
        assert(memcmp(&bad,&state,sizeof(bad))==0);
        assert(memcmp(saved,pcm,sizeof(saved))==0);
        assert(produced==71u);
    }
    puts("PASS AAC PS-4c1: 32+6 real QMF lookahead, 20/34 stereo, three delayed frames, rollback");
    return 0;
}
