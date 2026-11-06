/* SBR-54u: verify actual original-owner source_for_target
 * statistics for 2- and 6-slot FIXVAR; not a QMF/PCM test.
 */
#include "sauti_aac_sbr_hf_patch_route_audit.h"
#include "sauti_aac_sbr_master.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void make_frame(SautiAacSbrPayloadFrame *f){
    SautiAacSbrMaster master;
    memset(f,0,sizeof(*f));
    assert(sauti_aac_sbr_master_linear(16u,32u,1u,0u,&master));
    assert(sauti_aac_sbr_tables_build(&master,2u,&f->tables));
    assert(sauti_aac_sbr_patch_build(
           48000u,&master,&f->tables,&f->patches));
    f->grid.env_count=1u;f->grid.noise_count=1u;
    f->grid.env_border[0]=0u;f->grid.env_border[1]=16u;
    f->grid.noise_border[0]=0u;f->grid.noise_border[1]=16u;
    f->energy.env_count=1u;f->energy.noise_count=1u;
    f->energy.freq_res[0]=1u;
    f->env_edges[0]=0u;f->env_edges[1]=32u;
    f->noise_edges[0]=0u;f->noise_edges[1]=32u;
    f->limiter_mode=2u;f->gain_mode=2u;
    for(unsigned k=0u;k<f->tables.n_high;k++)
        f->energy.env_energy[0][k]=64.0;
    for(unsigned k=0u;k<f->tables.n_noise;k++)
        f->energy.noise_factor[0][k]=0.25;
}
static void test(unsigned tail){
    SautiAacSbrPayloadFrame prior,current,third;
    make_frame(&prior);make_frame(&current);make_frame(&third);
    prior.grid.env_border[1]=(uint8_t)(16u+tail/2u);
    prior.grid.noise_border[1]=(uint8_t)(16u+tail/2u);
    prior.env_edges[1]=(uint8_t)(32u+tail);
    prior.noise_edges[1]=(uint8_t)(32u+tail);
    current.grid.env_border[0]=(uint8_t)(tail/2u);
    current.grid.noise_border[0]=(uint8_t)(tail/2u);
    current.env_edges[0]=(uint8_t)tail;
    current.noise_edges[0]=(uint8_t)tail;
    SautiAacSbrTemporalHistory history={0};
    SautiAacSbrTemporalHandoff handoff;
    SautiAacSbrHfPatchRouteAuditState state={0};
    assert(sauti_aac_sbr_temporal_history_push(
           &prior,&history,&handoff));
    assert(sauti_aac_sbr_hf_patch_route_audit_add(
           &history,&state));
    for(unsigned b=0u;b<4u;b++){
        const unsigned k=SAUTI_AAC_SBR_ROUTE_BAND_FIRST+b;
        const int src=prior.patches.source_for_target[k];
        assert(src>=0 && src<32);
        assert(state.frames==1u && state.mapped[b]==32u &&
               state.previous_owner[b]==0u &&
               state.outside[b]==0u && state.unmapped[b]==0u &&
               state.sources[b][(unsigned)src]==32u);
    }
    assert(sauti_aac_sbr_temporal_history_push(
           &current,&history,&handoff));
    assert(handoff.previous_tail_slots==tail);
    assert(sauti_aac_sbr_hf_patch_route_audit_add(
           &history,&state));
    for(unsigned b=0u;b<4u;b++){
        const unsigned k=SAUTI_AAC_SBR_ROUTE_BAND_FIRST+b;
        const int src=prior.patches.source_for_target[k];
        assert(state.frames==2u && state.mapped[b]==64u &&
               state.previous_owner[b]==tail &&
               state.sources[b][(unsigned)src]==64u);
    }
    const SautiAacSbrHfPatchRouteAuditState snapshot=state;
    assert(!sauti_aac_sbr_hf_patch_route_audit_add(
           &history,&state));
    assert(memcmp(&snapshot,&state,sizeof(state))==0);
    SautiAacSbrTemporalHistory bad=history;
    bad.current.patches.source_for_target[18u]=31;
    assert(!sauti_aac_sbr_hf_patch_route_audit_add(
           &bad,&state));
    assert(memcmp(&snapshot,&state,sizeof(state))==0);
    assert(sauti_aac_sbr_temporal_history_push(
           &third,&history,&handoff));
    bad=history;
    bad.current.patches.source_for_target[18u]=-2;
    assert(!sauti_aac_sbr_hf_patch_route_audit_add(
           &bad,&state));
    assert(memcmp(&snapshot,&state,sizeof(state))==0);
    assert(sauti_aac_sbr_hf_patch_route_audit_add(
           &history,&state));
    for(unsigned b=0u;b<4u;b++)
        assert(state.mapped[b]==96u &&
               state.previous_owner[b]==tail);
    printf("SBR-54u: FIXVAR %u original-owner target18..21 "
           "source band and atomic rollback PASS\n",tail);
}
int main(void){
    test(2u);test(6u);
    puts("PASS SBR-54u HF patch route audit (no PCM)");
    return 0;
}
