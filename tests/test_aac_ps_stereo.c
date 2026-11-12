#include "sauti_aac_ps_stereo.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void prepare(SautiAacPsHybridSchedule *schedule,
                    SautiAacPsComplexMatrix *matrix,unsigned mode){
    SautiAacPsFrame frame={0};SautiAacPsMappedFrame mapped={0};
    frame.resolved_num_env=1u;frame.border_position[0]=31u;
    mapped.bands=mode;mapped.env_count=1u;
    assert(sauti_aac_ps_hybrid_schedule_build(&frame,&mapped,schedule));
    memset(matrix,0,sizeof(*matrix));
    matrix->bands=mode;matrix->env_count=1u;
    for(unsigned b=0;b<mode;b++){
        matrix->coefficient[0][b].h11.re=1.0f;
        matrix->coefficient[0][b].h22.re=1.0f;
    }
}
static int near_complex(SautiAacSbrComplex a,SautiAacSbrComplex b){
    return fabsf(a.re-b.re)<2e-5f&&fabsf(a.im-b.im)<2e-5f;
}
int main(void){
    SautiAacPsHybridSchedule schedule={0};
    SautiAacPsComplexMatrix matrix={0};
    SautiAacSbrComplex qmf[38u*64u],left[32u*91u+1u],right[32u*91u+1u];
    SautiAacSbrComplex low[32u*91u],diffuse[32u*91u];
    SautiAacPsStereoState state={0},independent={0};
    for(unsigned mode=20u;mode<=34u;mode+=14u){
        const unsigned bands=mode==20u?71u:91u;
        const size_t n=(size_t)32u*bands;
        prepare(&schedule,&matrix,mode);
        memset(&state,0,sizeof(state));memset(&independent,0,sizeof(independent));
        for(unsigned frame=0;frame<3u;frame++){
            for(unsigned s=0;s<38u;s++)for(unsigned b=0;b<64u;b++){
                const int z=(int)((frame*31u+s*29u+b*23u)%131u)-65;
                qmf[(size_t)s*64u+b]=(SautiAacSbrComplex){z*0.007f,z*-0.004f};
            }
            left[n]=(SautiAacSbrComplex){123.0f,234.0f};
            right[n]=(SautiAacSbrComplex){345.0f,456.0f};
            assert(sauti_aac_ps_qmf_bank_analyze(&schedule,qmf,38u*64u,
                                                  &independent.qmf_low,low,n));
            assert(sauti_aac_ps_decorrelator_apply(&schedule,low,n,
                                                    &independent.decorrelator,diffuse,n));
            assert(sauti_aac_ps_stereo_hybrid_apply(&schedule,&matrix,qmf,38u*64u,
                                                    &state,left,n,right,n));
            for(size_t i=0;i<n;i++){
                assert(near_complex(left[i],low[i]));
                assert(near_complex(right[i],diffuse[i]));
            }
            assert(memcmp(&state,&independent,sizeof(state))==0);
            assert(left[n].re==123.0f&&right[n].im==456.0f);
        }
        /* Failure during DSP processing must preserve BOTH outputs and state. */
        SautiAacPsStereoState old=state;
        SautiAacSbrComplex lsave[32u*91u],rsave[32u*91u];
        memcpy(lsave,left,sizeof(lsave));memcpy(rsave,right,sizeof(rsave));
        qmf[0].re=NAN;
        assert(!sauti_aac_ps_stereo_hybrid_apply(&schedule,&matrix,qmf,38u*64u,
                                                 &state,left,n,right,n));
        assert(memcmp(&old,&state,sizeof(state))==0);
        assert(memcmp(lsave,left,sizeof(lsave))==0);
        assert(memcmp(rsave,right,sizeof(rsave))==0);
        qmf[0].re=0.0f;
        /* Invalid matrix must be rejected before updating any state. */
        matrix.coefficient[0][0].h11.re=NAN;
        assert(!sauti_aac_ps_stereo_hybrid_apply(&schedule,&matrix,qmf,38u*64u,
                                                 &state,left,n,right,n));
        assert(memcmp(&old,&state,sizeof(state))==0);
        matrix.coefficient[0][0].h11.re=1.0f;
        assert(!sauti_aac_ps_stereo_hybrid_apply(&schedule,&matrix,qmf,38u*64u-1u,
                                                 &state,left,n,right,n));
        assert(!sauti_aac_ps_stereo_hybrid_apply(&schedule,&matrix,qmf,38u*64u,
                                                 &state,left,n-1u,right,n));
        assert(!sauti_aac_ps_stereo_hybrid_apply(&schedule,&matrix,qmf,38u*64u,
                                                 &state,left,n,left,n));
        assert(memcmp(&old,&state,sizeof(state))==0);
    }
    puts("PASS AAC PS-3a: 20/34 QMF+decorrelator+complex mix, 3 frames, rollback");
    return 0;
}
