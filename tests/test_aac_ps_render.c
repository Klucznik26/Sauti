#include "sauti_aac_ps_render.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static SautiAacPsFrame frame20(void){
    SautiAacPsFrame frame={0};
    frame.resolved_num_env=2u;
    frame.border_position[0]=15u;
    frame.border_position[1]=31u;
    frame.iid_count=20u;
    frame.icc_count=20u;
    frame.ipdopd_count=11u;
    frame.enable_ipdopd=1u;
    frame.header.enable_iid=1u;
    frame.header.enable_icc=1u;
    frame.iid[0][0]=2;
    frame.icc[1][3]=4;
    frame.ipd[0][0]=2;
    frame.opd[0][0]=1;
    return frame;
}

int main(void){
    SautiAacPsRenderState state={0},saved_state;
    SautiAacPsRenderPlan plan={0},saved_plan;
    SautiAacPsFrame frame=frame20();

    assert(sauti_aac_ps_render_plan_build(&frame,&state,&plan));
    assert(state.frames==1u && state.phase.ready && state.phase.bands==20u);
    assert(plan.mapped.bands==20u && plan.mapped.env_count==2u &&
           plan.schedule.hybrid_bands==71u && plan.schedule.slot_to_env[15]==0u &&
           plan.schedule.slot_to_env[16]==1u && plan.matrix.bands==20u &&
           plan.complex_matrix.has_phase);

    frame=(SautiAacPsFrame){0};
    frame.resolved_num_env=1u;
    frame.border_position[0]=31u;
    frame.iid_count=34u;
    frame.icc_count=34u;
    frame.ipdopd_count=17u;
    frame.enable_ipdopd=1u;
    frame.header.enable_iid=1u;
    frame.header.enable_icc=1u;
    assert(sauti_aac_ps_render_plan_build(&frame,&state,&plan));
    assert(state.frames==2u && state.phase.bands==34u &&
           plan.mapped.bands==34u && plan.schedule.hybrid_bands==91u &&
           plan.complex_matrix.bands==34u);

    saved_state=state;
    saved_plan=plan;
    frame.iid_count=1u;
    assert(!sauti_aac_ps_render_plan_build(&frame,&state,&plan));
    assert(memcmp(&state,&saved_state,sizeof(state))==0 &&
           memcmp(&plan,&saved_plan,sizeof(plan))==0);
    puts("PASS AAC PS render plan: atomic parser-to-DSP integration");
    return 0;
}
