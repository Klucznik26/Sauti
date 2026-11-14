#include "sauti_aac_ps_decorrelator.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void setup(SautiAacPsHybridSchedule *s,unsigned mode){
    SautiAacPsFrame frame={0};
    SautiAacPsMappedFrame mapped={0};
    frame.resolved_num_env=1u;
    frame.border_position[0]=31u;
    mapped.bands=mode;
    mapped.env_count=1u;
    assert(sauti_aac_ps_hybrid_schedule_build(&frame,&mapped,s));
}
int main(void){
    SautiAacPsHybridSchedule schedule={0};
    SautiAacSbrComplex direct[32u*91u]={0},out[32u*91u+1u]={0};
    SautiAacSbrComplex reference[32u*91u]={0};
    SautiAacPsDecorrelatorState state={0},separate={0};
    float gains[32u*34u]={0};

    for(unsigned mode=20u;mode<=34u;mode+=14u){
        const unsigned bands=mode==20u?71u:91u;
        setup(&schedule,mode);
        memset(&state,0,sizeof(state));
        memset(&separate,0,sizeof(separate));
        for(unsigned frame=0;frame<3u;frame++){
            for(unsigned t=0;t<32u;t++)for(unsigned k=0;k<bands;k++){
                const unsigned n=(frame*19u+t*47u+k*31u)%109u;
                direct[t*bands+k]=(SautiAacSbrComplex){
                    (float)((int)n-50)*0.0125f,
                    (float)((int)n-43)*-0.008f
                };
            }
            out[32u*bands]=(SautiAacSbrComplex){123.0f,456.0f};
            assert(sauti_aac_ps_transient_gains(&schedule,direct,32u*bands,
                     &separate.transient,gains,32u*mode));
            assert(sauti_aac_ps_decorrelate(&schedule,direct,32u*bands,
                     &separate.decorrelation,reference,32u*bands));
            for(unsigned t=0;t<32u;t++)for(unsigned k=0;k<bands;k++){
                const float gain=gains[t*mode+schedule.hybrid_to_parameter[k]];
                reference[t*bands+k].re*=gain;
                reference[t*bands+k].im*=gain;
            }
            assert(sauti_aac_ps_decorrelator_apply(&schedule,direct,32u*bands,
                        &state,out,32u*bands));
            for(unsigned i=0;i<32u*bands;i++){
                assert(fabsf(reference[i].re-out[i].re)<1e-6f);
                assert(fabsf(reference[i].im-out[i].im)<1e-6f);
            }
            assert(out[32u*bands].re==123.0f);
            assert(memcmp(&state,&separate,sizeof(state))==0);
        }
        const SautiAacPsDecorrelatorState saved_state=state;
        SautiAacSbrComplex saved_out[32u*91u];
        memcpy(saved_out,out,sizeof(saved_out));
        direct[0].re=NAN;
        assert(!sauti_aac_ps_decorrelator_apply(&schedule,direct,32u*bands,
                    &state,out,32u*bands));
        assert(memcmp(&state,&saved_state,sizeof(state))==0);
        assert(memcmp(out,saved_out,sizeof(saved_out))==0);
        direct[0].re=0.0f;
        assert(!sauti_aac_ps_decorrelator_apply(&schedule,direct,32u*bands-1u,
                    &state,out,32u*bands));
        assert(!sauti_aac_ps_decorrelator_apply(&schedule,direct,32u*bands,
                    &state,out,32u*bands-1u));
        assert(memcmp(&state,&saved_state,sizeof(state))==0);
        assert(memcmp(out,saved_out,sizeof(saved_out))==0);
        /* Failure in PS-2b after PS-2a must roll back both states. */
        state.decorrelation.input_history[0][0].re=NAN;
        const SautiAacPsDecorrelatorState broken_state=state;
        assert(!sauti_aac_ps_decorrelator_apply(&schedule,direct,32u*bands,
                    &state,out,32u*bands));
        assert(memcmp(&state,&broken_state,sizeof(state))==0);
        assert(memcmp(out,saved_out,sizeof(saved_out))==0);
        state=saved_state;
    }
    puts("PASS AAC PS-2c: 20/34 transient+allpass, three frames, atomic rollback");
    return 0;
}
