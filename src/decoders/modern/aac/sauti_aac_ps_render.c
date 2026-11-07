#include "sauti_aac_ps_render.h"

int sauti_aac_ps_render_plan_build(const SautiAacPsFrame *frame,
                                   SautiAacPsRenderState *state,
                                   SautiAacPsRenderPlan *out){
    if(!frame || !state || !out || state->frames==UINT64_MAX ||
       state->phase.ready>1u ||
       (!state->frames && state->phase.ready) ||
       (state->frames && !state->phase.ready))return 0;

    SautiAacPsRenderState next_state=*state;
    SautiAacPsRenderPlan next;
    if(!sauti_aac_ps_map_frame(frame,&next.mapped) ||
       !sauti_aac_ps_hybrid_schedule_build(frame,&next.mapped,&next.schedule) ||
       !sauti_aac_ps_matrix_build(frame,&next.mapped,&next.matrix) ||
       !sauti_aac_ps_phase_apply(frame,&next.mapped,&next.matrix,
                                  &next_state.phase,&next.complex_matrix))
        return 0;
    next_state.frames++;
    *state=next_state;
    *out=next;
    return 1;
}
