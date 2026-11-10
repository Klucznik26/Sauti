#include "sauti_aac_ps_phase.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static int near(float a,float b){return fabsf(a-b)<0.00001f;}
int main(void){
    SautiAacPsFrame frame={0};
    SautiAacPsMappedFrame mapped={0};
    SautiAacPsMatrix matrix={0};
    SautiAacPsPhaseState state={0},saved_state;
    SautiAacPsComplexMatrix out={0},saved;
    frame.resolved_num_env=1u;frame.enable_ipdopd=1u;
    mapped.bands=20u;mapped.env_count=1u;
    matrix.bands=20u;matrix.env_count=1u;
    for(unsigned b=0u;b<20u;b++)matrix.coefficient[0][b]=(SautiAacPsMatrixCoefficients){1,1,1,1};
    mapped.ipd[0][0]=2;mapped.opd[0][0]=0;
    assert(sauti_aac_ps_phase_apply(&frame,&mapped,&matrix,&state,&out));
    assert(out.has_phase&&state.ready&&state.bands==20u&&
           near(out.coefficient[0][0].h11.re,1.0f)&&near(out.coefficient[0][0].h11.im,0.0f)&&
           near(out.coefficient[0][0].h12.re,0.6f)&&near(out.coefficient[0][0].h12.im,-0.8f)&&
           near(out.coefficient[0][0].h22.re,0.6f)&&near(out.coefficient[0][0].h22.im,-0.8f));
    frame.enable_ipdopd=0u;
    assert(sauti_aac_ps_phase_apply(&frame,&mapped,&matrix,&state,&out));
    assert(!out.has_phase&&near(out.coefficient[0][3].h11.re,1.0f)&&
           near(out.coefficient[0][3].h11.im,0.0f));
    saved=out;saved_state=state;
    frame.enable_ipdopd=1u;mapped.ipd[0][0]=8;
    assert(!sauti_aac_ps_phase_apply(&frame,&mapped,&matrix,&state,&out));
    assert(memcmp(&out,&saved,sizeof(out))==0&&memcmp(&state,&saved_state,sizeof(state))==0);
    mapped.ipd[0][0]=2;state=saved_state;state.bands=19u;
    saved=out;saved_state=state;
    assert(!sauti_aac_ps_phase_apply(&frame,&mapped,&matrix,&state,&out));
    assert(memcmp(&out,&saved,sizeof(out))==0&&memcmp(&state,&saved_state,sizeof(state))==0);
    puts("PASS AAC PS phase: IPD/OPD smoothing, valid topology and rollback");
    return 0;
}
