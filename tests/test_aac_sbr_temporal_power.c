/* SBR-53c: envelope power must include samples deferred into
 * the next QMF frame, rather than silently dropping their energy. */
#include "sauti_aac_sbr_temporal_power.h"
#include "sauti_aac_sbr_master.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void make_frame(SautiAacSbrPayloadFrame *f){
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
    f->limiter_mode=2u;f->gain_mode=2u;
    for(unsigned b=0;b<f->tables.n_high;b++)
        f->energy.env_energy[0][b]=64.0;
    for(unsigned b=0;b<f->tables.n_noise;b++)
        f->energy.noise_factor[0][b]=0.25;
}
static void extend(SautiAacSbrPayloadFrame *f,unsigned tail){
    assert(tail==2u || tail==6u);
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
static void fill_hf(SautiAacSbrComplex out[32u*64u],
                    const SautiAacSbrPayloadFrame *f,
                    float first,float remainder){
    memset(out,0,32u*64u*sizeof(out[0]));
    for(unsigned t=0;t<32u;t++)
        for(unsigned k=f->tables.kx;k<f->tables.kx+f->tables.m;k++)
            out[(size_t)t*64u+k].re=t<2u?first:remainder;
}
static void boundary_test(unsigned tail){
    SautiAacSbrPayloadFrame p,n;
    make_frame(&p);extend(&p,tail);
    make_frame(&n);follow(&n,tail);
    SautiAacSbrTemporalHistory history={0};
    SautiAacSbrTemporalHandoff handoff;
    SautiAacSbrTemporalPowerState power={0};
    SautiAacSbrTemporalPowerReport report;
    SautiAacSbrComplex hf[32u*64u];
    assert(sauti_aac_sbr_temporal_history_push(&p,&history,&handoff));
    fill_hf(hf,&p,2.0f,2.0f);
    assert(sauti_aac_sbr_temporal_power_measure(
        &history,hf,32u*64u,&power,&report));
    assert(report.completed_count==0u &&
           report.deferred_slots==tail && power.pending==1u);
    assert(power.pending_count==32u);
    assert(sauti_aac_sbr_temporal_history_push(&n,&history,&handoff));
    assert(handoff.previous_tail_slots==tail &&
           handoff.current_start_slots==tail &&
           handoff.overlapping_slots==0u);
    /* Source from the previous frame (inherited tail) deliberately
     * has different power from the 32-slot source that follows it. */
    fill_hf(hf,&n,10.0f,3.0f);
    for(unsigned t=0u;t<tail;t++)
        for(unsigned k=n.tables.kx;k<n.tables.kx+n.tables.m;k++)
            hf[(size_t)t*64u+k].re=10.0f;
    assert(sauti_aac_sbr_temporal_power_measure(
        &history,hf,32u*64u,&power,&report));
    assert(report.completed_count==2u &&
           report.deferred_slots==0u && !power.pending);
    assert(report.completed[0].source==0u &&
           report.completed[0].envelope==0u &&
           report.completed[0].slots==32u+tail);
    assert(report.completed[1].source==1u &&
           report.completed[1].envelope==0u &&
           report.completed[1].slots==32u-tail);
    const unsigned k=n.tables.kx;
    const double expected=(32.0*4.0+(double)tail*100.0)/
                          (32.0+(double)tail);
    assert(fabs(report.completed[0].mean_power[k]-expected)<1e-10);
    assert(fabs(report.completed[1].mean_power[k]-9.0)<1e-10);

    /* A late NaN in the next frame must not publish any partial
     * new envelope or advance the envelope-power history. */
    SautiAacSbrPayloadFrame third;
    make_frame(&third);
    assert(sauti_aac_sbr_temporal_history_push(
        &third,&history,&handoff));
    SautiAacSbrTemporalPowerState saved_state=power;
    SautiAacSbrTemporalPowerReport saved_report=report;
    fill_hf(hf,&third,1.0f,1.0f);
    hf[31u*64u+k].re=NAN;
    assert(!sauti_aac_sbr_temporal_power_measure(
        &history,hf,32u*64u,&power,&report));
    assert(memcmp(&power,&saved_state,sizeof(power))==0);
    assert(memcmp(&report,&saved_report,sizeof(report))==0);
    hf[31u*64u+k].re=1.0f;
    assert(sauti_aac_sbr_temporal_power_measure(
        &history,hf,32u*64u,&power,&report));
    assert(power.frames==3u && report.completed_count==1u);
    assert(fabs(report.completed[0].mean_power[k]-1.0)<1e-10);

    /* Prevent replaying the same frame and accepting a fabricated
     * temporal handoff without its genuine retained samples. */
    saved_state=power;saved_report=report;
    assert(!sauti_aac_sbr_temporal_power_measure(
        &history,hf,32u*64u,&power,&report));
    assert(memcmp(&power,&saved_state,sizeof(power))==0);
    assert(memcmp(&report,&saved_report,sizeof(report))==0);
    printf("SBR-53c: %u-slot tail counted in full HF envelope mean, atomic state PASS\n",tail);
}
int main(void){
    boundary_test(2u);
    boundary_test(6u);
    puts("PASS SBR-53c envelope power continuity (no HE-AAC PCM)");
    return 0;
}
