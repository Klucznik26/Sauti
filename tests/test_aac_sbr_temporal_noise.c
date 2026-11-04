/* SBR-53f: canonical ROM phase is consumed in EMITTED QMF
 * chronology, including the preceding frame's inherited slots. */
#include "sauti_aac_sbr_temporal_noise.h"
#include "sauti_aac_sbr_master.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void setup(SautiAacSbrPayloadFrame *f,
                  double energy,double noise){
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
    f->limiter_mode=0u;f->gain_mode=3u;
    for(unsigned b=0;b<f->tables.n_high;b++)
        f->energy.env_energy[0][b]=energy;
    for(unsigned b=0;b<f->tables.n_noise;b++)
        f->energy.noise_factor[0][b]=noise;
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
static void check_rom(const SautiAacSbrTemporalNoiseFrame *out,
                      unsigned slot,unsigned bin,
                      unsigned expected_index,double expected_amp){
    const size_t ix=(size_t)slot*64u+bin;
    SautiAacSbrComplex expected={0};
    assert(sauti_aac_sbr_noise_rom_lookup(
        expected_index&511u,&expected));
    assert(memcmp(&out->vector[ix],&expected,sizeof(expected))==0);
    assert(fabs(out->raw_amplitude[ix]-expected_amp)<1e-12);
}
static void tail_test(unsigned tail){
    SautiAacSbrPayloadFrame first,second;
    setup(&first,64.0,0.25);extend(&first,tail);
    setup(&second,144.0,1.0);follow(&second,tail);
    SautiAacSbrTemporalHistory history={0};
    SautiAacSbrTemporalHandoff handoff;
    SautiAacSbrTemporalNoiseState state={0};
    SautiAacSbrTemporalNoiseFrame out;
    SautiAacSbrTemporalNoiseReport report;
    SautiAacSbrTemporalApplyReport apply={0};

    assert(sauti_aac_sbr_temporal_history_push(
        &first,&history,&handoff));
    memset(&out,0x55,sizeof(out));
    SautiAacSbrTemporalNoiseFrame saved=out;
    apply.emitted=0u;
    apply.pending_slots=32u;
    assert(sauti_aac_sbr_temporal_noise_prepare(
        &history,&apply,&state,&out,&report));
    assert(!report.emitted && state.frames==1u &&
           state.emitted_frames==0u && state.rom.emitted==0u);
    assert(memcmp(&out,&saved,sizeof(out))==0);

    assert(sauti_aac_sbr_temporal_history_push(
        &second,&history,&handoff));
    assert(handoff.previous_tail_slots==tail &&
           handoff.current_start_slots==tail);
    apply.emitted=1u;apply.pending_slots=0u;apply.frame_index=0u;

    /* Wrong frame index, even with otherwise valid SBR side data,
     * must not move the phase or overwrite the caller's output. */
    SautiAacSbrTemporalNoiseState checkpoint=state;
    SautiAacSbrTemporalNoiseReport saved_report=report;
    apply.frame_index=99u;
    assert(!sauti_aac_sbr_temporal_noise_prepare(
        &history,&apply,&state,&out,&report));
    assert(memcmp(&state,&checkpoint,sizeof(state))==0);
    assert(memcmp(&report,&saved_report,sizeof(report))==0);
    assert(memcmp(&out,&saved,sizeof(out))==0);
    apply.frame_index=0u;

    const unsigned k=first.tables.kx,m=first.tables.m;
    const double amp0=sqrt(64.0)*sqrt(0.25/1.25);
    const double amp1=sqrt(144.0)*sqrt(1.0/2.0);
    assert(sauti_aac_sbr_temporal_noise_prepare(
        &history,&apply,&state,&out,&report));
    assert(report.emitted && report.frame_index==0u &&
           report.vectors==32u*m &&
           report.nonzero_amplitudes==32u*m &&
           state.rom.emitted==32u*m &&
           state.rom.index==(32u*m&511u));
    check_rom(&out,0u,k,1u,amp0);
    check_rom(&out,31u,k+m-1u,32u*m,amp0);

    /* A handoff of 2 or 6 slots MUST keep the previous frame's
     * amplitude for exactly those slots, while ROM phase proceeds. */
    SautiAacSbrTemporalNoiseState corrupt=state;
    corrupt.held_targets.slot[0].noise_factor[k]=NAN;
    SautiAacSbrTemporalNoiseFrame previous=out;
    saved_report=report;
    SautiAacSbrTemporalApplyReport end={0};
    end.emitted=1u;end.frame_index=1u;
    assert(!sauti_aac_sbr_temporal_noise_flush(
        &end,&corrupt,&out,&report));
    assert(memcmp(&out,&previous,sizeof(out))==0);
    assert(memcmp(&report,&saved_report,sizeof(report))==0);

    assert(sauti_aac_sbr_temporal_noise_flush(
        &end,&state,&out,&report));
    assert(state.closed && state.emitted_frames==2u &&
           state.rom.emitted==64u*m &&
           report.frame_index==1u && report.vectors==32u*m);
    check_rom(&out,0u,k,32u*m+1u,amp0);
    check_rom(&out,tail-1u,k,32u*m+(tail-1u)*m+1u,amp0);
    check_rom(&out,tail,k,32u*m+tail*m+1u,amp1);
    check_rom(&out,31u,k+m-1u,64u*m,amp1);
    checkpoint=state;previous=out;saved_report=report;
    assert(!sauti_aac_sbr_temporal_noise_flush(
        &end,&state,&out,&report));
    assert(memcmp(&state,&checkpoint,sizeof(state))==0);
    assert(memcmp(&out,&previous,sizeof(out))==0);
    assert(memcmp(&report,&saved_report,sizeof(report))==0);
    printf("SBR-53f: %u-slot ownership, ROM index continuity and rollback PASS\n",tail);
}
static void no_tail_test(void){
    SautiAacSbrPayloadFrame first;
    setup(&first,0.0,0.0);
    SautiAacSbrTemporalHistory history={0};
    SautiAacSbrTemporalHandoff handoff;
    SautiAacSbrTemporalNoiseState state={0};
    SautiAacSbrTemporalNoiseFrame out;
    SautiAacSbrTemporalNoiseReport report;
    SautiAacSbrTemporalApplyReport apply={0};
    assert(sauti_aac_sbr_temporal_history_push(
        &first,&history,&handoff));
    assert(sauti_aac_sbr_temporal_noise_prepare(
        &history,&apply,&state,&out,&report));
    assert(!report.emitted);
    apply.emitted=1u;apply.frame_index=0u;
    assert(sauti_aac_sbr_temporal_noise_flush(
        &apply,&state,&out,&report));
    assert(report.emitted && report.nonzero_amplitudes==0u &&
           report.vectors==32u*first.tables.m &&
           state.rom.emitted==32u*first.tables.m);
    puts("SBR-53f: zero-amplitude HF still advances noise ROM PASS");
}
int main(void){
    tail_test(2u);
    tail_test(6u);
    no_tail_test();
    puts("PASS SBR-53f delayed per-slot ROM noise schedule (no PCM)");
    return 0;
}
