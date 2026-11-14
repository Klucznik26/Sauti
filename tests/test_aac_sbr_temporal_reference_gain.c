/* SBR-54l: true SBR-53c temporal completed power + original-owner
 * reference HF/noise/sine limiter; synthetic 2/6-slot FIXVAR.
 * Production preview/decoder must not be modified by the audit. */
#include "sauti_aac_sbr_temporal_reference_gain.h"
#include "sauti_aac_sbr_master.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define COUNT (32u*64u)
static void make_frame(SautiAacSbrPayloadFrame *f,double energy){
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
    f->limiter_mode=0u;f->gain_mode=3u;
    f->transient_env=1u; /* no transient in this one-env frame */
    f->harmonic[0]=1u;
    /* build_one lifts envelope energy by 2^30. Store the normalized
     * value the probe passes after SBR-60, not the raw quantizer. */
    for(unsigned b=0;b<f->tables.n_high;b++)
        f->energy.env_energy[0][b]=ldexp(energy,-30);
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
static void hf(SautiAacSbrComplex x[COUNT],
               const SautiAacSbrPayloadFrame *f,
               unsigned early,float first,float rest){
    memset(x,0,COUNT*sizeof(*x));
    for(unsigned t=0;t<32u;t++)
        for(unsigned k=f->tables.kx;k<f->tables.kx+f->tables.m;k++)
            x[(size_t)t*64u+k].re=t<early?first:rest;
}
static void run(unsigned tail){
    SautiAacSbrPayloadFrame before,now;
    make_frame(&before,64.0);extend(&before,tail);
    make_frame(&now,144.0);follow(&now,tail);
    now.transient_env=0u; /* delta=0 in the NEW owner only */
    now.harmonic[0]=0u;
    SautiAacSbrTemporalHistory history={0};
    SautiAacSbrTemporalHandoff handoff;
    SautiAacSbrTemporalPowerState state={0};
    SautiAacSbrTemporalPowerReport power={0};
    SautiAacSbrTemporalReferenceGainReport comparison={0};
    SautiAacSbrComplex x[COUNT];
    assert(sauti_aac_sbr_temporal_history_push(
           &before,&history,&handoff));
    hf(x,&before,32u,2.0f,2.0f);
    assert(sauti_aac_sbr_temporal_power_measure(
        &history,x,COUNT,&state,&power));
    assert(power.completed_count==0u);
    assert(sauti_aac_sbr_temporal_reference_gain_calculate(
        &history,&power,&comparison));
    assert(comparison.completed_count==0u);

    assert(sauti_aac_sbr_temporal_history_push(
           &now,&history,&handoff));
    hf(x,&now,tail,10.0f,3.0f);
    assert(sauti_aac_sbr_temporal_power_measure(
        &history,x,COUNT,&state,&power));
    assert(power.completed_count==2u);
    assert(sauti_aac_sbr_temporal_reference_gain_calculate(
        &history,&power,&comparison));
    assert(comparison.completed_count==2u);
    assert(comparison.completed[0].source==0u &&
           comparison.completed[0].slots==32u+tail &&
           comparison.completed[0].transient_delta==1u &&
           comparison.completed[0].limited.sine_bins==1u);
    assert(comparison.completed[1].source==1u &&
           comparison.completed[1].slots==32u-tail &&
           comparison.completed[1].transient_delta==0u &&
           comparison.completed[1].limited.sine_bins==0u);
    assert(comparison.noise_positive_bins>0u &&
           comparison.gain_changed_bins>0u &&
           comparison.old_gain_sq_sum>0.0 &&
           comparison.new_gain_sq_sum>0.0 &&
           comparison.group_count==2u);
    const unsigned k=now.tables.kx;
    const double mean=(32.0*4.0+(double)tail*100.0)/
                      (32.0+(double)tail);
    const SautiAacSbrGainLimitedFrame *prev=
        &comparison.completed[0].limited;
    const SautiAacSbrGainLimitedFrame *cur=
        &comparison.completed[1].limited;
    assert(prev->band[k].hf_gain>0.0);
    assert(cur->band[k].hf_gain>0.0);
    assert(fabs(power.completed[0].mean_power[k]-mean)<1e-10);
    assert(prev->band[k].noise_amplitude>0.0);
    assert(prev->band[k].sine_amplitude>=0.0);
    assert(comparison.completed[0].harmonic_bins==1u);

    /* A bad late original-owner energy must reject the entire
     * calculation without changing any prior diagnostic report. */
    const SautiAacSbrTemporalReferenceGainReport saved=comparison;
    SautiAacSbrTemporalHistory broken=history;
    broken.current.energy.env_energy[0][0]=NAN;
    assert(!sauti_aac_sbr_temporal_reference_gain_calculate(
        &broken,&power,&comparison));
    assert(memcmp(&saved,&comparison,sizeof(saved))==0);
    broken=history;
    broken.previous.energy.noise_factor[0][0]=-1.0;
    assert(!sauti_aac_sbr_temporal_reference_gain_calculate(
        &broken,&power,&comparison));
    assert(memcmp(&saved,&comparison,sizeof(saved))==0);
    SautiAacSbrTemporalPowerReport damaged=power;
    damaged.completed[0].slots--;
    assert(!sauti_aac_sbr_temporal_reference_gain_calculate(
        &history,&damaged,&comparison));
    assert(memcmp(&saved,&comparison,sizeof(saved))==0);
    damaged=power;
    damaged.completed[1].mean_power[63u]=NAN;
    assert(!sauti_aac_sbr_temporal_reference_gain_calculate(
        &history,&damaged,&comparison));
    assert(memcmp(&saved,&comparison,sizeof(saved))==0);

    printf("SBR-54l: %u-slot FIXVAR, source=previous/current, tonal/attack and rollback PASS\n",tail);
}
int main(void){
    run(2u);run(6u);
    puts("PASS SBR-54l real-envelope gain partition adapter (NO PCM)");
    return 0;
}
