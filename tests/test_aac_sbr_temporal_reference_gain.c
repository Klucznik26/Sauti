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
    f->transient_env=0u; /* no onset: the parser's convention (attack[0] clear); env_count would mean an onset carried into the next frame */
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
    now.attack[0]=1u;     /* delta=0 in the NEW owner only: a signaled onset is attack[env] (SBR-79), not env==transient_env */
    now.harmonic[0]=0u;
    SautiAacSbrTemporalHistory history={0};
    SautiAacSbrTemporalHandoff handoff;
    SautiAacSbrTemporalPowerState state={0};
    SautiAacSbrTemporalPowerReport power={0};
    SautiAacSbrTemporalReferenceGainReport comparison={0};
    SautiAacSbrComplex x[COUNT],previous_x[COUNT];
    assert(sauti_aac_sbr_temporal_history_push(
           &before,&history,&handoff));
    hf(x,&before,32u,2.0f,2.0f);
    assert(sauti_aac_sbr_temporal_power_measure(
        &history,x,COUNT,&state,&power));
    assert(power.completed_count==0u);
    assert(sauti_aac_sbr_temporal_reference_gain_calculate(
        &history,&power,x,COUNT,NULL,0u,NULL,0u,&comparison));
    assert(comparison.completed_count==0u);

    memcpy(previous_x,x,sizeof(previous_x));
    assert(sauti_aac_sbr_temporal_history_push(
           &now,&history,&handoff));
    hf(x,&now,tail,10.0f,3.0f);
    assert(sauti_aac_sbr_temporal_power_measure(
        &history,x,COUNT,&state,&power));
    assert(power.completed_count==2u);
    assert(sauti_aac_sbr_temporal_reference_gain_calculate(
        &history,&power,x,COUNT,previous_x,COUNT,NULL,0u,&comparison));
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
    if(tail==6u){
        /* Envelope [6,32) is flat amplitude 3. The HF-generator
         * window [0,26) also includes the six leading 10s, so its
         * mean is 30 and the limiter must follow that, not 9. */
        const unsigned kx=now.tables.kx;
        const unsigned k2=kx+now.tables.m;
        double window[64]={0},energy_b[64]={0},noise_b[64]={0};
        double raw_e[64]={0},raw_c[64]={0},raw_mean[64]={0};
        uint8_t sine[64]={0},mapped[64]={0};
        assert(sauti_aac_sbr_temporal_power_hfgen_window(
            x,COUNT,NULL,0u,6u,32u,kx,k2,window));
        assert(fabs(window[kx]-30.0)<1e-9);
        assert(fabs(power.completed[1].mean_power[kx]-9.0)<1e-9);
        SautiAacSbrLimiterBands bands;
        assert(sauti_aac_sbr_limiter_bands_build(
            &now.tables,&now.patches,now.limiter_mode,&bands));
        for(unsigned b=0;b<now.tables.n_high;b++)
            for(unsigned k=now.tables.high[b];k<now.tables.high[b+1u];k++)
                energy_b[k]=ldexp(144.0,-30);
        for(unsigned b=0;b<now.tables.n_noise;b++)
            for(unsigned k=now.tables.noise[b];k<now.tables.noise[b+1u];k++)
                noise_b[k]=0.25;
        for(unsigned k=kx;k<k2;k++){
            raw_e[k]=ldexp(energy_b[k],30);
            raw_c[k]=ldexp(window[k],30);
            raw_mean[k]=ldexp(power.completed[1].mean_power[k],30);
        }
        SautiAacSbrGainLimitedFrame from_window,from_mean;
        assert(sauti_aac_sbr_gain_limit_partition_flags(
            raw_e,raw_c,noise_b,sine,mapped,bands.edges,bands.count,
            now.gain_mode,0u,&from_window));
        assert(sauti_aac_sbr_gain_limit_partition_flags(
            raw_e,raw_mean,noise_b,sine,mapped,bands.edges,bands.count,
            now.gain_mode,0u,&from_mean));
        const double got=comparison.completed[1].limited.band[kx].hf_gain;
        const double other=from_mean.band[kx].hf_gain;
        assert(fabs(got-from_window.band[kx].hf_gain)<1e-9);
        assert(fabs(got-other)>1e-3*fmax(got,other));
    }

    /* A bad late original-owner energy must reject the entire
     * calculation without changing any prior diagnostic report. */
    const SautiAacSbrTemporalReferenceGainReport saved=comparison;
    SautiAacSbrTemporalHistory broken=history;
    broken.current.energy.env_energy[0][0]=NAN;
    assert(!sauti_aac_sbr_temporal_reference_gain_calculate(
        &broken,&power,x,COUNT,previous_x,COUNT,NULL,0u,&comparison));
    assert(memcmp(&saved,&comparison,sizeof(saved))==0);
    broken=history;
    broken.previous.energy.noise_factor[0][0]=-1.0;
    assert(!sauti_aac_sbr_temporal_reference_gain_calculate(
        &broken,&power,x,COUNT,previous_x,COUNT,NULL,0u,&comparison));
    assert(memcmp(&saved,&comparison,sizeof(saved))==0);
    SautiAacSbrTemporalPowerReport damaged=power;
    damaged.completed[0].slots--;
    assert(!sauti_aac_sbr_temporal_reference_gain_calculate(
        &history,&damaged,x,COUNT,previous_x,COUNT,NULL,0u,&comparison));
    assert(memcmp(&saved,&comparison,sizeof(saved))==0);
    damaged=power;
    damaged.completed[1].mean_power[63u]=NAN;
    assert(!sauti_aac_sbr_temporal_reference_gain_calculate(
        &history,&damaged,x,COUNT,previous_x,COUNT,NULL,0u,&comparison));
    assert(memcmp(&saved,&comparison,sizeof(saved))==0);

    printf("SBR-54l: %u-slot FIXVAR, source=previous/current, tonal/attack and rollback PASS\n",tail);
}
/* SBR-79: the parser stores transient_env=0 for "no signaled onset" and leaves attack[0] clear.  That must give delta=1 (FFmpeg
 * e_a[1]=-1), not delta=0 as the old env==transient_env rule did.  Returns the delta of the new owner's envelope. */
static unsigned new_owner_delta(unsigned attack0,unsigned transient_env,unsigned before_transient){
    SautiAacSbrPayloadFrame before,now;
    make_frame(&before,64.0);extend(&before,6u);
    before.transient_env=before_transient;
    make_frame(&now,144.0);follow(&now,6u);
    now.transient_env=transient_env;
    now.attack[0]=(uint8_t)attack0;
    now.harmonic[0]=0u;
    SautiAacSbrTemporalHistory history={0};
    SautiAacSbrTemporalHandoff handoff;
    SautiAacSbrTemporalPowerState state={0};
    SautiAacSbrTemporalPowerReport power={0};
    SautiAacSbrTemporalReferenceGainReport comparison={0};
    SautiAacSbrComplex x[COUNT],previous_x[COUNT];
    assert(sauti_aac_sbr_temporal_history_push(&before,&history,&handoff));
    hf(x,&before,32u,2.0f,2.0f);
    assert(sauti_aac_sbr_temporal_power_measure(&history,x,COUNT,&state,&power));
    memcpy(previous_x,x,sizeof(previous_x));
    assert(sauti_aac_sbr_temporal_history_push(&now,&history,&handoff));
    hf(x,&now,6u,10.0f,3.0f);
    assert(sauti_aac_sbr_temporal_power_measure(&history,x,COUNT,&state,&power));
    assert(power.completed_count==2u);
    assert(sauti_aac_sbr_temporal_reference_gain_calculate(
        &history,&power,x,COUNT,previous_x,COUNT,NULL,0u,&comparison));
    assert(comparison.completed[1].source==1u);
    return comparison.completed[1].transient_delta;
}
int main(void){
    run(2u);run(6u);
    assert(new_owner_delta(0u,0u,0u)==1u);   /* no onset: parser gives transient_env=0, attack[0]=0 */
    assert(new_owner_delta(1u,0u,0u)==0u);   /* signaled onset */
    assert(new_owner_delta(0u,1u,0u)==1u);   /* transient_env of the NEW frame past its last envelope does not change its own envelope 0 */
    /* e_a[0]: the previous frame's onset index equals its envelope count (1) -> it falls in this frame's envelope 0 -> delta=0,
     * although attack[0] is clear.  FFmpeg: e_a[0] = -(e_a[1] != bs_num_env_old). */
    assert(new_owner_delta(0u,0u,1u)==0u);
    assert(new_owner_delta(0u,0u,0u)==1u);   /* same frame without the carried onset */
    {   /* sauti_aac_sbr_env_delta on a 3-envelope frame: the carried onset reaches envelope 0 only */
        SautiAacSbrPayloadFrame f,prev;
        memset(&f,0,sizeof(f));memset(&prev,0,sizeof(prev));
        f.grid.env_count=3u;prev.grid.env_count=3u;
        prev.transient_env=3u;                                    /* previous onset index == its envelope count */
        assert(sauti_aac_sbr_env_delta(&f,0u,&prev)==0u);
        assert(sauti_aac_sbr_env_delta(&f,1u,&prev)==1u);
        assert(sauti_aac_sbr_env_delta(&f,2u,&prev)==1u);
        assert(sauti_aac_sbr_env_delta(&f,0u,NULL)==1u);          /* predecessor unknown: nothing carried */
        prev.transient_env=2u;                                    /* an onset inside the previous frame is not carried */
        assert(sauti_aac_sbr_env_delta(&f,0u,&prev)==1u);
        prev.transient_env=4u;                                    /* out of range (the parser never emits it): not a carried onset */
        assert(sauti_aac_sbr_env_delta(&f,0u,&prev)==1u);
        prev.grid.env_count=0u;prev.transient_env=0u;             /* empty predecessor must not look like a carried onset */
        assert(sauti_aac_sbr_env_delta(&f,0u,&prev)==1u);
        f.attack[1]=1u;
        assert(sauti_aac_sbr_env_delta(&f,1u,NULL)==0u);
        assert(sauti_aac_sbr_env_delta(&f,2u,NULL)==1u);
    }
    puts("SBR-79: delta follows attack[env] and the carried onset e_a[0]; transient_env=0 means no onset PASS");
    puts("PASS SBR-54l real-envelope gain partition adapter (NO PCM)");
    return 0;
}
