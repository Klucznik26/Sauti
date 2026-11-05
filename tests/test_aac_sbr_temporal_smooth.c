/* SBR-53h: five-tap amplitude smoothing in delayed 32-slot QMF
 * order, including inherited temporal tails and transient bypass. */
#include "sauti_aac_sbr_temporal_smooth.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define SLOTS 32u
#define BANDS 64u
#define ACTIVE_K 16u
#define ACTIVE_M 8u

static void frame(SautiAacSbrTemporalNoiseFrame *noise,
                  SautiAacSbrTemporalNoiseReport *report,
                  unsigned frame_index,unsigned tail){
    memset(noise,0,sizeof(*noise));
    memset(report,0,sizeof(*report));
    report->emitted=1u;
    report->frame_index=frame_index;
    for(unsigned t=0u;t<SLOTS;t++){
        /* Frame zero: uniform 2.0. Frame one: 2.0 inherited
         * for the first tail slots, followed by 6.0. */
        const double a=frame_index==0u || t<tail?2.0:6.0;
        for(unsigned k=ACTIVE_K;k<ACTIVE_K+ACTIVE_M;k++){
            const unsigned ix=t*BANDS+k;
            noise->active[ix]=1u;
            noise->raw_amplitude[ix]=a;
            report->vectors++;
            report->nonzero_amplitudes++;
        }
    }
}
static void failure(
    const SautiAacSbrTemporalNoiseFrame *noise,
    const SautiAacSbrTemporalNoiseReport *report,
    SautiAacSbrTemporalSmoothState *state,
    SautiAacSbrTemporalSmoothFrame *output,
    SautiAacSbrTemporalSmoothReport *stats){
    const SautiAacSbrTemporalSmoothState previous=*state;
    const SautiAacSbrTemporalSmoothFrame old_output=*output;
    const SautiAacSbrTemporalSmoothReport old_stats=*stats;
    assert(!sauti_aac_sbr_temporal_noise_smooth(
        noise,report,state,output,stats));
    assert(memcmp(&previous,state,sizeof(previous))==0);
    assert(memcmp(&old_output,output,sizeof(old_output))==0);
    assert(memcmp(&old_stats,stats,sizeof(old_stats))==0);
}
static void handoff(unsigned tail){
    SautiAacSbrTemporalNoiseFrame noise;
    SautiAacSbrTemporalNoiseReport n_report;
    SautiAacSbrTemporalSmoothState state={0};
    SautiAacSbrTemporalSmoothFrame smooth;
    SautiAacSbrTemporalSmoothReport stats;
    frame(&noise,&n_report,0u,tail);
    assert(sauti_aac_sbr_temporal_noise_smooth(
        &noise,&n_report,&state,&smooth,&stats));
    assert(state.ready==1u && state.frames==1u &&
           stats.active_bins==SLOTS*ACTIVE_M);
    for(unsigned t=0u;t<SLOTS;t++)
        assert(fabs(smooth.amplitude[t*BANDS+ACTIVE_K]-2.0)<1e-12);

    frame(&noise,&n_report,1u,tail);
    /* Signal transient in the second source for one slot: bypass the
     * FIR, but still insert the raw 6.0 into its history. */
    noise.smooth_bypass[tail+1u]=1u;
    const SautiAacSbrTemporalSmoothState before=state;
    SautiAacSbrTemporalNoiseFrame invalid=noise;
    invalid.raw_amplitude[31u*BANDS+ACTIVE_K]=NAN;
    failure(&invalid,&n_report,&state,&smooth,&stats);
    invalid=noise;
    invalid.active[31u*BANDS+ACTIVE_K]=2u;
    failure(&invalid,&n_report,&state,&smooth,&stats);
    invalid=noise;
    invalid.smooth_bypass[31u]=2u;
    failure(&invalid,&n_report,&state,&smooth,&stats);
    SautiAacSbrTemporalNoiseReport invalid_report=n_report;
    invalid_report.frame_index=2u;
    failure(&noise,&invalid_report,&state,&smooth,&stats);
    invalid_report=n_report;
    invalid_report.vectors--;
    failure(&noise,&invalid_report,&state,&smooth,&stats);
    assert(memcmp(&state,&before,sizeof(before))==0);

    assert(sauti_aac_sbr_temporal_noise_smooth(
        &noise,&n_report,&state,&smooth,&stats));
    assert(stats.frame_index==1u &&
           stats.active_bins==SLOTS*ACTIVE_M &&
           stats.bypassed_slots==1u);
    for(unsigned t=0u;t<tail;t++)
        assert(fabs(smooth.amplitude[t*BANDS+ACTIVE_K]-2.0)<1e-12);
    static const double h[5]={
        0.33333333333333,0.30150283239582,
        0.21816949906249,0.11516383427084,0.03183050093751
    };
    const double switch_value=h[0]*6.0+
        (h[1]+h[2]+h[3]+h[4])*2.0;
    assert(fabs(smooth.amplitude[tail*BANDS+ACTIVE_K]-
                switch_value)<1e-12);
    assert(fabs(smooth.amplitude[(tail+1u)*BANDS+ACTIVE_K]-
                6.0)<1e-12);
    const double continued=6.0*(h[0]+h[1]+h[2])+
                           2.0*(h[3]+h[4]);
    assert(fabs(smooth.amplitude[(tail+2u)*BANDS+ACTIVE_K]-
                continued)<1e-12);
    /* No duplicate processing of an already-committed frame. */
    failure(&noise,&n_report,&state,&smooth,&stats);
    printf("SBR-53h: %u-slot delayed switch, 5 taps, bypass and rollback PASS\n",tail);
}
static void zero_and_restart(void){
    SautiAacSbrTemporalNoiseFrame n={0};
    SautiAacSbrTemporalNoiseReport r={0};
    SautiAacSbrTemporalSmoothState s={0};
    SautiAacSbrTemporalSmoothFrame out;
    SautiAacSbrTemporalSmoothReport stats;
    r.emitted=1u;r.frame_index=0u;
    assert(sauti_aac_sbr_temporal_noise_smooth(
        &n,&r,&s,&out,&stats));
    assert(stats.active_bins==0u && stats.nonzero_amplitudes==0u);
    for(unsigned t=0u;t<32u*64u;t++)
        assert(out.amplitude[t]==0.0);
    r.frame_index=1u;r.vectors=1u;r.nonzero_amplitudes=1u;
    n.active[16u]=1u;n.raw_amplitude[16u]=9.0;
    assert(sauti_aac_sbr_temporal_noise_smooth(
        &n,&r,&s,&out,&stats));
    assert(fabs(out.amplitude[16u]-9.0)<1e-12);
    puts("SBR-53h: empty active map and first-active bin startup PASS");
}
int main(void){
    handoff(2u);
    handoff(6u);
    zero_and_restart();
    puts("PASS SBR-53h delayed 5-tap noise coefficient smoothing (no PCM)");
    return 0;
}
