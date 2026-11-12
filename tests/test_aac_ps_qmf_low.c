#include "sauti_aac_ps_qmf_low.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void fill_schedule(SautiAacPsHybridSchedule *s,unsigned mode){
    SautiAacPsFrame f={0};SautiAacPsMappedFrame m={0};
    f.resolved_num_env=1u;f.border_position[0]=31u;
    m.bands=mode;m.env_count=1u;
    assert(sauti_aac_ps_hybrid_schedule_build(&f,&m,s));
}
static void check_zero(const SautiAacSbrComplex *out,unsigned count){
    for(unsigned i=0;i<count;i++)assert(out[i].re==0.0f&&out[i].im==0.0f);
}
int main(void){
    SautiAacPsHybridSchedule s={0};
    SautiAacSbrComplex input[38u*64u]={0},out[32u*32u+1u]={0};
    SautiAacPsQmfLowState state={0};
    for(unsigned mode=20u;mode<=34u;mode+=14u){
        fill_schedule(&s,mode);
        const unsigned bands=mode==20u?10u:32u,source=mode==20u?3u:5u;
        memset(&state,0,sizeof(state));memset(out,0xA5,sizeof(out));
        assert(sauti_aac_ps_qmf_low_analyze(&s,input,38u*64u,&state,out,32u*bands));
        check_zero(out,32u*bands);
        assert(state.initialized==1u);
        /* Out-of-prefix sentinel: no clobber. */
        assert(memcmp(&out[32u*bands],&(SautiAacSbrComplex){NAN,NAN},0u)==0);
        for(unsigned t=0;t<38u;t++)for(unsigned b=0;b<source;b++){
            input[t*64u+b]=(SautiAacSbrComplex){0,0};
        }
        memset(&state,0,sizeof(state));
        /* A center impulse tests QMF0's 13-tap middle coefficient. */
        input[0]=(SautiAacSbrComplex){1,0};
        assert(sauti_aac_ps_qmf_low_analyze(&s,input,38u*64u,&state,out,32u*bands));
        const float center=mode==20u?0.125f:(float)(1.0/12.0);
        if(mode==20u){
            assert(fabsf(out[0].re-center)<1e-6f);
            assert(fabsf(out[1].re-center)<1e-6f);
            assert(fabsf(out[4].re-2.0f*center)<1e-6f);
            assert(fabsf(out[5].re-2.0f*center)<1e-6f);
        }else{
            for(unsigned i=0;i<12u;i++)assert(fabsf(out[i].re-center)<1e-6f);
        }
        /* Failed extraction cannot advance history or touch output. */
        SautiAacPsQmfLowState saved_state=state;
        SautiAacSbrComplex saved_out[32u*32u];
        memcpy(saved_out,out,sizeof(saved_out));
        assert(!sauti_aac_ps_qmf_low_analyze(&s,input,38u*64u,&state,out,32u*bands-1u));
        assert(memcmp(&state,&saved_state,sizeof(state))==0);
        assert(memcmp(saved_out,out,sizeof(saved_out))==0);
        input[0].im=NAN;
        assert(!sauti_aac_ps_qmf_low_analyze(&s,input,38u*64u,&state,out,32u*bands));
        assert(memcmp(&state,&saved_state,sizeof(state))==0);
        assert(memcmp(saved_out,out,sizeof(saved_out))==0);
        input[0].im=0;
        /* A sample at slot 31 must become the last history element. */
        input[0].re=0;
        input[31u*64u]=(SautiAacSbrComplex){1,0};
        memset(&state,0,sizeof(state));
        assert(sauti_aac_ps_qmf_low_analyze(&s,input,38u*64u,&state,out,32u*bands));
        assert(state.history[0][5].re==1.0f);
        /* The lookahead [32..37] does NOT become next-frame history. */
        input[31u*64u].re=0;
        input[37u*64u].re=5.0f;
        memset(out,0,sizeof(out));
        assert(sauti_aac_ps_qmf_low_analyze(&s,input,38u*64u,&state,out,32u*bands));
        assert(fabsf(out[0].re)>0.001f);
        assert(state.history[0][5].re==0.0f);
        input[37u*64u].re=0.0f;
        /* Finite input/state guards, and valid schedule checks. */
        state.initialized=2u;
        assert(!sauti_aac_ps_qmf_low_analyze(&s,input,38u*64u,&state,out,32u*bands));
        state.initialized=1u;
        s.slot_to_env[0]=1u;
        assert(!sauti_aac_ps_qmf_low_analyze(&s,input,38u*64u,&state,out,32u*bands));
    }
    puts("PASS AAC PS-1b: hybrid 6+2+2 / 12+8+4+4+4, 13 taps, overlap and rollback");
    return 0;
}
