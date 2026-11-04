/* SBR-53e: one-frame-delayed use of FULL original HF envelope
 * gains, without publishing HE-AAC PCM. */
#include "sauti_aac_sbr_temporal_apply.h"
#include "sauti_aac_sbr_master.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void make_frame(SautiAacSbrPayloadFrame *f,double energy){
    SautiAacSbrMaster master;
    memset(f,0,sizeof(*f));
    assert(sauti_aac_sbr_master_linear(16u,32u,1u,0u,&master));
    assert(sauti_aac_sbr_tables_build(&master,2u,&f->tables));
    assert(sauti_aac_sbr_patch_build(48000u,&master,
                                    &f->tables,&f->patches));
    f->grid.env_count=1u;
    f->grid.noise_count=1u;
    f->grid.env_border[0]=0u;f->grid.env_border[1]=16u;
    f->grid.noise_border[0]=0u;f->grid.noise_border[1]=16u;
    f->energy.env_count=1u;f->energy.noise_count=1u;
    f->energy.freq_res[0]=1u;
    f->env_edges[0]=0u;f->env_edges[1]=32u;
    f->noise_edges[0]=0u;f->noise_edges[1]=32u;
    f->limiter_mode=0u;f->gain_mode=3u;
    for(unsigned b=0;b<f->tables.n_high;b++)
        f->energy.env_energy[0][b]=energy;
    for(unsigned b=0;b<f->tables.n_noise;b++)
        f->energy.noise_factor[0][b]=0.25;
}
static void extend(SautiAacSbrPayloadFrame *f,unsigned tail){
    f->grid.env_border[1]=(uint8_t)(16u+tail/2u);
    f->grid.noise_border[1]=(uint8_t)(16u+tail/2u);
    f->env_edges[1]=(uint8_t)(32u+tail);
    f->noise_edges[1]=(uint8_t)(32u+tail);
}
static void follow(SautiAacSbrPayloadFrame *f,unsigned tail){
    f->grid.env_border[0]=(uint8_t)(tail/2u);
    f->grid.noise_border[0]=(uint8_t)(tail/2u);
    f->env_edges[0]=(uint8_t)tail;
    f->noise_edges[0]=(uint8_t)tail;
}
static void fill_hf(SautiAacSbrComplex high[32u*64u],
                    const SautiAacSbrPayloadFrame *f,
                    unsigned early,float a,float b){
    memset(high,0,32u*64u*sizeof(high[0]));
    for(unsigned t=0u;t<32u;t++)
        for(unsigned k=f->tables.kx;k<f->tables.kx+f->tables.m;k++)
            high[(size_t)t*64u+k].re=t<early?a:b;
}
static void rollback_test(
    const SautiAacSbrTemporalHistory *history,
    const SautiAacSbrComplex *high,
    const SautiAacSbrTemporalGainReport *gain,
    SautiAacSbrTemporalApplyState *state,
    SautiAacSbrComplex output[32u*64u],
    SautiAacSbrTemporalApplyReport *report){
    SautiAacSbrTemporalApplyState saved_state=*state;
    SautiAacSbrComplex saved_output[32u*64u];
    memcpy(saved_output,output,sizeof(saved_output));
    const SautiAacSbrTemporalApplyReport saved_report=*report;
    SautiAacSbrTemporalGainReport broken=*gain;
    broken.completed[0].applied_gain[history->previous.tables.kx]=NAN;
    assert(!sauti_aac_sbr_temporal_gain_apply(
        history,high,32u*64u,&broken,state,output,32u*64u,report));
    assert(memcmp(state,&saved_state,sizeof(saved_state))==0);
    assert(memcmp(output,saved_output,sizeof(saved_output))==0);
    assert(memcmp(report,&saved_report,sizeof(saved_report))==0);
    SautiAacSbrComplex invalid[32u*64u];
    memcpy(invalid,high,sizeof(invalid));
    invalid[31u*64u+history->current.tables.kx].re=NAN;
    assert(!sauti_aac_sbr_temporal_gain_apply(
        history,invalid,32u*64u,gain,state,output,32u*64u,report));
    assert(memcmp(state,&saved_state,sizeof(saved_state))==0);
    assert(memcmp(output,saved_output,sizeof(saved_output))==0);
    assert(memcmp(report,&saved_report,sizeof(saved_report))==0);
}
static void test_handoff(unsigned tail){
    SautiAacSbrPayloadFrame prev,cur;
    make_frame(&prev,64.0);extend(&prev,tail);
    make_frame(&cur,144.0);follow(&cur,tail);
    SautiAacSbrTemporalHistory history={0};
    SautiAacSbrTemporalHandoff handoff;
    SautiAacSbrTemporalPowerState power={0};
    SautiAacSbrTemporalPowerReport powers;
    SautiAacSbrTemporalGainReport gains;
    SautiAacSbrTemporalApplyState state={0};
    SautiAacSbrTemporalApplyReport report;
    SautiAacSbrComplex high[32u*64u],output[32u*64u];
    memset(output,0x55,sizeof(output));
    SautiAacSbrComplex untouched[32u*64u];
    memcpy(untouched,output,sizeof(untouched));

    assert(sauti_aac_sbr_temporal_history_push(
        &prev,&history,&handoff));
    fill_hf(high,&prev,32u,2.0f,2.0f);
    assert(sauti_aac_sbr_temporal_power_measure(
        &history,high,32u*64u,&power,&powers));
    assert(sauti_aac_sbr_temporal_gain_calculate(
        &history,&powers,&gains));
    assert(gains.completed_count==0u);
    assert(sauti_aac_sbr_temporal_gain_apply(
        &history,high,32u*64u,&gains,
        &state,output,32u*64u,&report));
    assert(!report.emitted && state.frames==1u &&
           state.pending_slots==32u);
    assert(memcmp(output,untouched,sizeof(output))==0);
    SautiAacSbrTemporalApplyState snapshot=state;
    SautiAacSbrTemporalApplyReport old_report=report;
    assert(!sauti_aac_sbr_temporal_gain_apply_flush(
        &state,output,32u*64u,&report));
    assert(memcmp(&state,&snapshot,sizeof(state))==0);
    assert(memcmp(output,untouched,sizeof(output))==0);
    assert(memcmp(&report,&old_report,sizeof(report))==0);

    assert(sauti_aac_sbr_temporal_history_push(
        &cur,&history,&handoff));
    fill_hf(high,&cur,tail,10.0f,3.0f);
    assert(sauti_aac_sbr_temporal_power_measure(
        &history,high,32u*64u,&power,&powers));
    assert(sauti_aac_sbr_temporal_gain_calculate(
        &history,&powers,&gains));
    assert(gains.completed_count==2u);
    rollback_test(&history,high,&gains,&state,output,&report);
    assert(sauti_aac_sbr_temporal_gain_apply(
        &history,high,32u*64u,&gains,
        &state,output,32u*64u,&report));
    assert(report.emitted==1u && report.frame_index==0u &&
           state.frames==2u && state.pending_slots==0u);
    const unsigned k=prev.tables.kx;
    const double mean=(32.0*4.0+(double)tail*100.0)/
                      (32.0+(double)tail);
    const double first_gain=sqrt(64.0/mean);
    for(unsigned t=0u;t<32u;t++)
        assert(fabs((double)output[(size_t)t*64u+k].re-
                    2.0*first_gain)<1e-5);

    /* The second frame starts with INHERITED gain (previous owner),
     * then switches to its own gain without discontinuity of owner. */
    assert(sauti_aac_sbr_temporal_gain_apply_flush(
        &state,output,32u*64u,&report));
    assert(report.emitted==1u && report.frame_index==1u &&
           state.closed==1u);
    for(unsigned t=0u;t<32u;t++){
        const double expected=t<tail?10.0*first_gain:12.0;
        assert(fabs((double)output[(size_t)t*64u+k].re-
                    expected)<1e-5);
    }
    memcpy(untouched,output,sizeof(untouched));
    snapshot=state;old_report=report;
    assert(!sauti_aac_sbr_temporal_gain_apply_flush(
        &state,output,32u*64u,&report));
    assert(memcmp(&state,&snapshot,sizeof(state))==0);
    assert(memcmp(output,untouched,sizeof(output))==0);
    assert(memcmp(&report,&old_report,sizeof(report))==0);
    puts(tail==2u?
       "SBR-53e: 2-slot lookahead gains applied to held HF QMF, flush PASS":
       "SBR-53e: 6-slot lookahead gains applied to held HF QMF, flush PASS");
}
static void test_no_tail(void){
    SautiAacSbrPayloadFrame f;
    make_frame(&f,64.0);
    SautiAacSbrTemporalHistory history={0};
    SautiAacSbrTemporalHandoff handoff;
    SautiAacSbrTemporalPowerState power={0};
    SautiAacSbrTemporalPowerReport powers;
    SautiAacSbrTemporalGainReport gains;
    SautiAacSbrTemporalApplyState state={0};
    SautiAacSbrTemporalApplyReport report;
    SautiAacSbrComplex high[32u*64u],out[32u*64u];
    fill_hf(high,&f,32u,2.0f,2.0f);
    assert(sauti_aac_sbr_temporal_history_push(
        &f,&history,&handoff));
    assert(sauti_aac_sbr_temporal_power_measure(
        &history,high,32u*64u,&power,&powers));
    assert(sauti_aac_sbr_temporal_gain_calculate(
        &history,&powers,&gains));
    assert(sauti_aac_sbr_temporal_gain_apply(
        &history,high,32u*64u,&gains,
        &state,out,32u*64u,&report));
    assert(!report.emitted && state.pending_slots==0u);
    SautiAacSbrTemporalApplyState invalid=state;
    const unsigned k=f.tables.kx;
    invalid.slot_gain[0u][k]=1e308; /* would overflow finite float */
    SautiAacSbrComplex saved[32u*64u];
    memcpy(saved,out,sizeof(saved));
    SautiAacSbrTemporalApplyReport saved_report=report;
    assert(!sauti_aac_sbr_temporal_gain_apply_flush(
        &invalid,out,32u*64u,&report));
    assert(memcmp(out,saved,sizeof(saved))==0);
    assert(memcmp(&report,&saved_report,sizeof(report))==0);
    assert(sauti_aac_sbr_temporal_gain_apply_flush(
        &state,out,32u*64u,&report));
    assert(report.emitted && report.frame_index==0u);
    assert(fabs(out[k].re-8.0f)<1e-5);
    puts("SBR-53e: single final frame and finite-float overflow rollback PASS");
}
int main(void){
    test_handoff(2u);
    test_handoff(6u);
    test_no_tail();
    puts("PASS SBR-53e buffered HF gain adjustment (no HE-AAC PCM)");
    return 0;
}
