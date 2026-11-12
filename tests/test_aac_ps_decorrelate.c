#include "sauti_aac_ps_decorrelate.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static void build(SautiAacPsHybridSchedule *schedule,unsigned mode){
    SautiAacPsFrame f={0};SautiAacPsMappedFrame m={0};
    f.resolved_num_env=1u;f.border_position[0]=31u;
    m.bands=mode;m.env_count=1u;
    assert(sauti_aac_ps_hybrid_schedule_build(&f,&m,schedule));
}
int main(void){
    SautiAacPsHybridSchedule schedule={0};
    SautiAacSbrComplex input[32u*91u],out[32u*91u+1u];
    SautiAacPsDecorrelateState state={0};
    for(unsigned mode=20u;mode<=34u;mode+=14u){
        const unsigned bands=mode==20u?71u:91u;
        const unsigned ap=mode==20u?30u:50u;
        const unsigned sh=mode==20u?42u:62u;
        build(&schedule,mode);
        memset(input,0,sizeof(input));memset(&state,0,sizeof(state));
        out[32u*bands]=(SautiAacSbrComplex){77.0f,88.0f};
        /* One impulse per route; delay paths have exact time indices. */
        input[ap]=(SautiAacSbrComplex){1.0f,0.5f};
        input[sh]=(SautiAacSbrComplex){2.0f,-0.25f};
        input[0]=(SautiAacSbrComplex){0.75f,-0.5f};
        assert(sauti_aac_ps_decorrelate(&schedule,input,32u*bands,&state,
                                         out,32u*bands));
        assert(state.bands==bands);
        for(unsigned n=0;n<32u;n++){
            SautiAacSbrComplex a=out[n*bands+ap];
            SautiAacSbrComplex b=out[n*bands+sh];
            assert(a.re==(n==14u?1.0f:0.0f));
            assert(a.im==(n==14u?0.5f:0.0f));
            assert(b.re==(n==1u?2.0f:0.0f));
            assert(b.im==(n==1u?-0.25f:0.0f));
        }
        assert(out[32u*bands].re==77.0f);
        /* Input at slot 0 can contribute only after a 2-slot delay
         * in the all-pass route; later echoes may follow. */
        assert(out[0].re==0.0f&&out[bands].re==0.0f);
        assert(isfinite(out[2u*bands].re)&&isfinite(out[2u*bands].im));
        /* Long-term history persists: impulse in last slot appears next
         * frame after one slot in short-delay bands. */
        memset(input,0,sizeof(input));
        input[31u*bands+sh]=(SautiAacSbrComplex){3.0f,4.0f};
        assert(sauti_aac_ps_decorrelate(&schedule,input,32u*bands,&state,
                                         out,32u*bands));
        memset(input,0,sizeof(input));
        assert(sauti_aac_ps_decorrelate(&schedule,input,32u*bands,&state,
                                         out,32u*bands));
        assert(out[sh].re==3.0f&&out[sh].im==4.0f);
        const SautiAacPsDecorrelateState saved=state;
        SautiAacSbrComplex copy[32u*91u];
        memcpy(copy,out,sizeof(copy));
        input[0].re=NAN;
        assert(!sauti_aac_ps_decorrelate(&schedule,input,32u*bands,&state,
                                          out,32u*bands));
        assert(memcmp(&state,&saved,sizeof(state))==0);
        assert(memcmp(out,copy,sizeof(copy))==0);
        input[0].re=0.0f;
        assert(!sauti_aac_ps_decorrelate(&schedule,input,32u*bands-1u,&state,
                                          out,32u*bands));
        assert(!sauti_aac_ps_decorrelate(&schedule,input,32u*bands,&state,
                                          out,32u*bands-1u));
        schedule.hybrid_to_parameter[0]=(uint8_t)mode;
        assert(!sauti_aac_ps_decorrelate(&schedule,input,32u*bands,&state,
                                          out,32u*bands));
        assert(memcmp(&state,&saved,sizeof(state))==0);
    }
    puts("PASS AAC PS-2b: 20/34 all-pass, 14/1 delays, history and atomicity");
    return 0;
}
