/* SBR-53d: full cross-frame limiter means, not clipped-frame gains. */
#include "sauti_aac_sbr_temporal_gain.h"
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
static void test_tail(unsigned tail){
    SautiAacSbrPayloadFrame prev,cur;
    make_frame(&prev,64.0);extend(&prev,tail);
    make_frame(&cur,144.0);follow(&cur,tail);
    SautiAacSbrTemporalHistory history={0};
    SautiAacSbrTemporalHandoff handoff;
    SautiAacSbrTemporalPowerState power_state={0};
    SautiAacSbrTemporalPowerReport powers;
    SautiAacSbrTemporalGainReport gains;
    SautiAacSbrComplex high[32u*64u];

    assert(sauti_aac_sbr_temporal_history_push(
           &prev,&history,&handoff));
    fill_hf(high,&prev,32u,2.0f,2.0f);
    assert(sauti_aac_sbr_temporal_power_measure(
        &history,high,32u*64u,&power_state,&powers));
    assert(powers.completed_count==0u);
    assert(sauti_aac_sbr_temporal_gain_calculate(
        &history,&powers,&gains));
    assert(gains.completed_count==0u);

    assert(sauti_aac_sbr_temporal_history_push(
           &cur,&history,&handoff));
    assert(handoff.previous_tail_slots==tail);
    fill_hf(high,&cur,tail,10.0f,3.0f);
    assert(sauti_aac_sbr_temporal_power_measure(
        &history,high,32u*64u,&power_state,&powers));
    assert(powers.completed_count==2u);
    assert(sauti_aac_sbr_temporal_gain_calculate(
        &history,&powers,&gains));
    assert(gains.completed_count==2u);
    const unsigned k=cur.tables.kx;
    const double mean=(32.0*4.0+(double)tail*100.0)/
                      (32.0+(double)tail);
    assert(gains.completed[0].source==0u &&
           gains.completed[0].slots==32u+tail &&
           gains.completed[0].limiter_count==1u);
    assert(gains.completed[1].source==1u &&
           gains.completed[1].slots==32u-tail &&
           gains.completed[1].limiter_count==1u);
    assert(fabs(gains.completed[0].applied_gain[k]-
                sqrt(64.0/mean))<1e-10);
    assert(fabs(gains.completed[1].applied_gain[k]-4.0)<1e-10);
    assert(gains.completed[0].noise_only_bands==0u &&
           gains.completed[1].noise_only_bands==0u);

    /* A source-less positive target can only be handled by a later
     * noise/sine stage; report signal gain 0, never invent energy. */
    SautiAacSbrTemporalPowerReport noise_only=powers;
    noise_only.completed[1].mean_power[k]=0.0;
    assert(sauti_aac_sbr_temporal_gain_calculate(
        &history,&noise_only,&gains));
    assert(gains.completed[1].noise_only_bands==1u &&
           gains.completed[1].applied_gain[k]==0.0);
    SautiAacSbrTemporalGainReport snapshot=gains;
    SautiAacSbrTemporalHistory bad=history;
    bad.current.energy.noise_factor[0][0]=0.0;
    assert(!sauti_aac_sbr_temporal_gain_calculate(
        &bad,&noise_only,&gains));
    assert(memcmp(&gains,&snapshot,sizeof(gains))==0);
    SautiAacSbrTemporalPowerReport broken=powers;
    broken.completed[0].mean_power[k]=NAN;
    assert(!sauti_aac_sbr_temporal_gain_calculate(
        &history,&broken,&gains));
    assert(memcmp(&gains,&snapshot,sizeof(gains))==0);
    broken=powers;
    broken.completed[0].slots--;
    assert(!sauti_aac_sbr_temporal_gain_calculate(
        &history,&broken,&gains));
    assert(memcmp(&gains,&snapshot,sizeof(gains))==0);
    broken=powers;
    broken.completed[0].source=1u;
    assert(!sauti_aac_sbr_temporal_gain_calculate(
        &history,&broken,&gains));
    assert(memcmp(&gains,&snapshot,sizeof(gains))==0);
    printf("SBR-53d: full %u-slot handoff gain cap/boost and zero-source rollback PASS\n",tail);
}
int main(void){
    test_tail(2u);
    test_tail(6u);
    puts("PASS SBR-53d limiter decisions from complete temporal HF envelope powers (no PCM)");
    return 0;
}
