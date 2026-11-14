#include "sauti_aac_ps_transient.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void schedule_make(SautiAacPsHybridSchedule *s,unsigned bands){
    SautiAacPsFrame frame={0};
    SautiAacPsMappedFrame mapped={0};
    frame.resolved_num_env=1u;
    frame.border_position[0]=31u;
    mapped.bands=bands;
    mapped.env_count=1u;
    assert(sauti_aac_ps_hybrid_schedule_build(&frame,&mapped,s));
}

int main(void){
    SautiAacPsHybridSchedule schedule={0};
    SautiAacSbrComplex input[32u*91u]={0};
    float gains[32u*34u+1u];
    SautiAacPsTransientState state={0};
    for(unsigned mode=20u;mode<=34u;mode+=14u){
        const unsigned hybrid=mode==20u?71u:91u;
        schedule_make(&schedule,mode);
        memset(&state,0,sizeof(state));
        memset(input,0,sizeof(input));
        gains[32u*mode]=777.0f;
        assert(sauti_aac_ps_transient_gains(&schedule,input,32u*hybrid,
                                             &state,gains,32u*mode));
        for(unsigned i=0;i<32u*mode;i++)assert(gains[i]==1.0f);
        assert(state.bands==mode);
        assert(gains[32u*mode]==777.0f);
        /* Sustained energy raises smoothed energy and peak history. */
        for(unsigned n=0;n<32u;n++)
            for(unsigned k=0;k<hybrid;k++)
                input[n*hybrid+k]=(SautiAacSbrComplex){0.1f,0.0f};
        assert(sauti_aac_ps_transient_gains(&schedule,input,32u*hybrid,
                                             &state,gains,32u*mode));
        for(unsigned b=0;b<mode;b++){
            assert(state.peak[b]>0.0f&&state.smooth[b]>0.0f);
            for(unsigned n=0;n<32u;n++){
                assert(gains[n*mode+b]>=0.0f&&gains[n*mode+b]<=1.0f);
            }
        }
        /* Silence after energy contains a transient-suppressed interval. */
        memset(input,0,sizeof(input));
        assert(sauti_aac_ps_transient_gains(&schedule,input,32u*hybrid,
                                             &state,gains,32u*mode));
        unsigned suppressed=0;
        for(unsigned i=0;i<32u*mode;i++)if(gains[i]<1.0f)suppressed++;
        assert(suppressed>0u);
        /* Error input must not alter persistent state or output. */
        const SautiAacPsTransientState saved=state;
        float copy[32u*34u];
        memcpy(copy,gains,sizeof(copy));
        input[0].re=NAN;
        assert(!sauti_aac_ps_transient_gains(&schedule,input,32u*hybrid,
                                              &state,gains,32u*mode));
        assert(memcmp(&saved,&state,sizeof(saved))==0);
        assert(memcmp(copy,gains,sizeof(copy))==0);
        input[0].re=0.0f;
        assert(!sauti_aac_ps_transient_gains(&schedule,input,32u*hybrid-1u,
                                              &state,gains,32u*mode));
        assert(memcmp(&saved,&state,sizeof(saved))==0);
        assert(memcmp(copy,gains,sizeof(copy))==0);
        assert(!sauti_aac_ps_transient_gains(&schedule,input,32u*hybrid,
                                              &state,gains,32u*mode-1u));
        assert(memcmp(&saved,&state,sizeof(saved))==0);
        schedule.hybrid_to_parameter[0]=(uint8_t)mode;
        assert(!sauti_aac_ps_transient_gains(&schedule,input,32u*hybrid,
                                              &state,gains,32u*mode));
        assert(memcmp(&saved,&state,sizeof(saved))==0);
        assert(memcmp(copy,gains,sizeof(copy))==0);
    }
    puts("PASS AAC PS-2a transient energy: 20/34 routing, continuity, silence, rollback");
    return 0;
}
