#include "sauti_aac_he_ps_bridge.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static SautiAacSbrPayloadFrame fixture(unsigned mode){
    SautiAacSbrPayloadFrame p={0};
    p.has_ps=1u;
    p.ps.resolved_num_env=1u;
    p.ps.border_position[0]=31u;
    p.ps.iid_count=mode;
    p.ps.icc_count=mode;
    p.ps.header.enable_iid=1u;
    p.ps.header.enable_icc=1u;
    p.ps.iid[0][0]=2;
    p.ps.icc[0][0]=3;
    return p;
}
int main(void){
    SautiAacHePsBridgeState state={0},independent={0};
    SautiAacSbrPayloadFrame payload;
    SautiAacSbrComplex qmf[38u*64u];
    float actual[4096u+1u]={0},expected[4096u];
    for(unsigned mode=20u;mode<=34u;mode+=14u){
        memset(&state,0,sizeof(state));memset(&independent,0,sizeof(independent));
        payload=fixture(mode);
        for(unsigned f=0;f<2u;f++){
            for(unsigned t=0;t<38u;t++)for(unsigned k=0;k<64u;k++){
                const int v=(int)((f*31u+t*17u+k*29u)%103u)-51;
                qmf[t*64u+k]=(SautiAacSbrComplex){v*0.004f,v*-0.002f};
            }
            SautiAacPsRenderPlan plan;
            assert(sauti_aac_ps_render_plan_build(&payload.ps,&independent.render,&plan));
            assert(sauti_aac_ps_pcm_stereo_from_mono_qmf(&plan.schedule,
                       &plan.complex_matrix,qmf,38u*64u,
                       &independent.pcm,expected,4096u));
            independent.frames++;
            actual[4096u]=199.0f;
            assert(sauti_aac_he_ps_bridge_render(&payload,qmf,38u*64u,
                                                  &state,actual,4096u));
            assert(memcmp(actual,expected,sizeof(expected))==0);
            assert(actual[4096u]==199.0f);
            assert(memcmp(&state,&independent,sizeof(state))==0);
        }
        SautiAacHePsBridgeState saved=state;
        float old[4096u+1u];
        memcpy(old,actual,sizeof(old));
        payload.has_ps=0u;
        assert(!sauti_aac_he_ps_bridge_render(&payload,qmf,38u*64u,
                                               &state,actual,4096u));
        payload.has_ps=1u;
        payload.ps.iid_count=1u;
        assert(!sauti_aac_he_ps_bridge_render(&payload,qmf,38u*64u,
                                               &state,actual,4096u));
        payload.ps.iid_count=mode;
        qmf[0].re=NAN;
        assert(!sauti_aac_he_ps_bridge_render(&payload,qmf,38u*64u,
                                               &state,actual,4096u));
        qmf[0].re=0.0f;
        assert(!sauti_aac_he_ps_bridge_render(&payload,qmf,38u*64u-1u,
                                               &state,actual,4096u));
        assert(!sauti_aac_he_ps_bridge_render(&payload,qmf,38u*64u,
                                               &state,actual,4095u));
        assert(memcmp(&state,&saved,sizeof(state))==0);
        assert(memcmp(actual,old,sizeof(old))==0);
        state.frames++;
        saved=state;
        assert(!sauti_aac_he_ps_bridge_render(&payload,qmf,38u*64u,
                                               &state,actual,4096u));
        assert(memcmp(&state,&saved,sizeof(state))==0);
        assert(memcmp(actual,old,sizeof(old))==0);
    }
    puts("PASS AAC PS-4a: parsed PS payload -> native stereo PCM, 20/34, rollback");
    return 0;
}
