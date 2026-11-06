/* SBR-54s: 6-slot cross-frame lookback of actual complex LOW QMF,
 * with independent original FIXVAR patch-map owner. NO PCM.
 */
#include "sauti_aac_sbr_temporal_source6.h"
#include "sauti_aac_sbr_master.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define LOW  (32u*32u)
#define HIGH (32u*64u)

static void make_frame(SautiAacSbrPayloadFrame *f){
    SautiAacSbrMaster master;
    memset(f,0,sizeof(*f));
    assert(sauti_aac_sbr_master_linear(16u,32u,1u,0u,&master));
    assert(sauti_aac_sbr_tables_build(&master,2u,&f->tables));
    assert(sauti_aac_sbr_patch_build(48000u,&master,
                                    &f->tables,&f->patches));
    f->grid.env_count=1u;f->grid.noise_count=1u;
    f->grid.env_border[0]=0u;f->grid.env_border[1]=16u;
    f->grid.noise_border[0]=0u;f->grid.noise_border[1]=16u;
    f->energy.env_count=1u;f->energy.noise_count=1u;
    f->energy.freq_res[0]=1u;
    f->env_edges[0]=0u;f->env_edges[1]=32u;
    f->noise_edges[0]=0u;f->noise_edges[1]=32u;
    f->limiter_mode=2u;f->gain_mode=2u;
    for(unsigned b=0u;b<f->tables.n_high;b++)
        f->energy.env_energy[0][b]=64.0;
    for(unsigned b=0u;b<f->tables.n_noise;b++)
        f->energy.noise_factor[0][b]=0.25;
}

static void low_fill(SautiAacSbrComplex low[LOW],unsigned frame){
    for(unsigned t=0;t<32u;t++)
        for(unsigned k=0;k<32u;k++){
            const unsigned i=t*32u+k;
            const float v=(float)(frame*2048u+t*32u+k+1u);
            low[i].re=v;
            low[i].im=-v;
        }
}

static void verify_output(const SautiAacSbrTemporalHistory *history,
                          const SautiAacSbrComplex *aligned,
                          const SautiAacSbrComplex *high){
    SautiAacSbrTemporalHfTargets targets;
    assert(sauti_aac_sbr_temporal_hf_targets(history,&targets));
    for(unsigned t=0;t<32u;t++){
        SautiAacSbrComplex expected[64u];
        const SautiAacSbrTemporalHfSlot *owner=&targets.slot[t];
        const SautiAacSbrPayloadFrame *frame=
            owner->source==0u?&history->previous:&history->current;
        assert(sauti_aac_sbr_hf_transpose(
               &frame->patches,&frame->tables,
               aligned+(size_t)t*32u,32u,1u,expected,64u));
        assert(memcmp(expected,high+(size_t)t*64u,
                      64u*sizeof(expected[0]))==0);
    }
}

static void test_tail(unsigned tail){
    SautiAacSbrPayloadFrame first_frame,second_frame;
    make_frame(&first_frame);
    make_frame(&second_frame);
    first_frame.grid.env_border[1]=(uint8_t)(16u+tail/2u);
    first_frame.grid.noise_border[1]=(uint8_t)(16u+tail/2u);
    first_frame.env_edges[1]=(uint8_t)(32u+tail);
    first_frame.noise_edges[1]=(uint8_t)(32u+tail);
    second_frame.grid.env_border[0]=(uint8_t)(tail/2u);
    second_frame.grid.noise_border[0]=(uint8_t)(tail/2u);
    second_frame.env_edges[0]=(uint8_t)tail;
    second_frame.noise_edges[0]=(uint8_t)tail;

    SautiAacSbrTemporalHistory history={0};
    SautiAacSbrTemporalHandoff handoff;
    SautiAacSbrTemporalSource6State state={0};
    SautiAacSbrTemporalSource6Report report={0};
    SautiAacSbrComplex frame0[LOW],frame1[LOW],
                       aligned[LOW],high[HIGH];
    low_fill(frame0,0u);low_fill(frame1,1u);
    assert(sauti_aac_sbr_temporal_history_push(
           &first_frame,&history,&handoff));
    assert(sauti_aac_sbr_temporal_source6_prepare(
           &history,frame0,LOW,&state,
           aligned,LOW,high,HIGH,&report));
    assert(report.frame_index==0u &&
           report.carried_from_previous==0u &&
           report.missing_preroll==6u &&
           report.previous_owner_slots==0u &&
           report.current_source_slots==26u);
    for(unsigned t=0;t<6u;t++)
        for(unsigned k=0;k<32u;k++)
            assert(aligned[t*32u+k].re==0.0f &&
                   aligned[t*32u+k].im==0.0f);
    assert(memcmp(aligned+6u*32u,frame0,26u*32u*
                  sizeof(frame0[0]))==0);
    assert(memcmp(state.previous_last6,frame0+26u*32u,
                  sizeof(state.previous_last6))==0);
    verify_output(&history,aligned,high);

    assert(sauti_aac_sbr_temporal_history_push(
           &second_frame,&history,&handoff));
    assert(handoff.previous_tail_slots==tail &&
           handoff.current_start_slots==tail);
    assert(sauti_aac_sbr_temporal_source6_prepare(
           &history,frame1,LOW,&state,
           aligned,LOW,high,HIGH,&report));
    assert(state.frames==2u &&
           report.frame_index==1u &&
           report.carried_from_previous==6u &&
           report.missing_preroll==0u &&
           report.current_source_slots==26u &&
           report.previous_owner_slots==tail);
    assert(memcmp(aligned,frame0+26u*32u,
                  6u*32u*sizeof(frame0[0]))==0);
    assert(memcmp(aligned+6u*32u,frame1,
                  26u*32u*sizeof(frame1[0]))==0);
    assert(memcmp(state.previous_last6,frame1+26u*32u,
                  sizeof(state.previous_last6))==0);
    verify_output(&history,aligned,high);

    /* Late corruption must not advance state or partially write. */
    SautiAacSbrTemporalSource6State saved_state=state;
    SautiAacSbrTemporalSource6Report saved_report=report;
    SautiAacSbrComplex saved_low[LOW],saved_high[HIGH];
    memcpy(saved_low,aligned,sizeof(saved_low));
    memcpy(saved_high,high,sizeof(saved_high));
    frame1[LOW-1u].im=NAN;
    assert(!sauti_aac_sbr_temporal_source6_prepare(
           &history,frame1,LOW,&state,
           aligned,LOW,high,HIGH,&report));
    frame1[LOW-1u].im=-frame1[LOW-1u].re;
    assert(memcmp(&state,&saved_state,sizeof(state))==0);
    assert(memcmp(&report,&saved_report,sizeof(report))==0);
    assert(memcmp(aligned,saved_low,sizeof(saved_low))==0);
    assert(memcmp(high,saved_high,sizeof(saved_high))==0);

    /* Calling without a new temporal_history_push is a replay. */
    assert(!sauti_aac_sbr_temporal_source6_prepare(
           &history,frame1,LOW,&state,
           aligned,LOW,high,HIGH,&report));
    assert(memcmp(&state,&saved_state,sizeof(state))==0);
    assert(memcmp(&report,&saved_report,sizeof(report))==0);
    SautiAacSbrTemporalSource6State malformed=state;
    malformed.previous_last6[5u].im=INFINITY;
    assert(!sauti_aac_sbr_temporal_source6_prepare(
           &history,frame1,LOW,&malformed,
           aligned,LOW,high,HIGH,&report));
    assert(memcmp(aligned,saved_low,sizeof(saved_low))==0);

    printf("SBR-54s: six QMF lookback slots, FIXVAR %u owner slots, "
           "source boundary, replay and rollback PASS\n",tail);
}

int main(void){
    test_tail(2u);
    test_tail(6u);
    puts("PASS SBR-54s original-owner six-slot source QMF (no PCM)");
    return 0;
}
