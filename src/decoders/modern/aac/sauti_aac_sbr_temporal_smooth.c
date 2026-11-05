/* SBR-53h: delayed per-bin 5-tap noise amplitude smoother.
 * Same h_smooth coefficients as SBR-21. This does not create PCM. */
#include "sauti_aac_sbr_temporal_smooth.h"
#include <math.h>
#include <string.h>

#define NSLOTS 32u
#define NBANDS 64u
#define NCOEFF (NSLOTS*NBANDS)

static int state_ok(const SautiAacSbrTemporalSmoothState *state){
    if(state->ready>1u || (!state->ready && state->frames) ||
       (state->ready && state->frames==0u) ||
       state->frames==UINT64_MAX)return 0;
    for(unsigned k=0u;k<NBANDS;k++){
        if(state->seen[k]>1u)return 0;
        for(unsigned j=0u;j<4u;j++)
            if(!isfinite(state->recent[j][k]) ||
               state->recent[j][k]<0.0)return 0;
    }
    return 1;
}

int sauti_aac_sbr_temporal_noise_smooth(
    const SautiAacSbrTemporalNoiseFrame *noise,
    const SautiAacSbrTemporalNoiseReport *noise_report,
    SautiAacSbrTemporalSmoothState *state,
    SautiAacSbrTemporalSmoothFrame *out,
    SautiAacSbrTemporalSmoothReport *report){
    if(!noise || !noise_report || !state || !out || !report ||
       !state_ok(state) || noise_report->emitted!=1u ||
       noise_report->frame_index!=state->frames ||
       noise_report->vectors>NCOEFF ||
       noise_report->nonzero_amplitudes>noise_report->vectors)
        return 0;

    static const double h[5]={
        0.33333333333333,0.30150283239582,
        0.21816949906249,0.11516383427084,0.03183050093751
    };
    SautiAacSbrTemporalSmoothState next=*state;
    SautiAacSbrTemporalSmoothFrame result={0};
    SautiAacSbrTemporalSmoothReport stats={0};
    stats.frame_index=noise_report->frame_index;

    for(unsigned t=0u;t<NSLOTS;t++){
        const unsigned bypass=noise->smooth_bypass[t];
        if(bypass>1u)return 0;
        if(bypass)stats.bypassed_slots++;
        for(unsigned k=0u;k<NBANDS;k++){
            const unsigned ix=t*NBANDS+k;
            const unsigned active=noise->active[ix];
            const double a=noise->raw_amplitude[ix];
            if(active>1u || !isfinite(a) || a<0.0)return 0;
            if(!active){
                if(a!=0.0)return 0;
                /* No pseudo-history across deactivated HF bins. */
                next.seen[k]=0u;
                for(unsigned j=0u;j<4u;j++)next.recent[j][k]=0.0;
                continue;
            }
            if(!next.seen[k]){
                for(unsigned j=0u;j<4u;j++)next.recent[j][k]=a;
                next.seen[k]=1u;
            }
            double smooth=a;
            if(!bypass){
                smooth=h[0]*a;
                for(unsigned j=0u;j<4u;j++)
                    smooth+=h[j+1u]*next.recent[3u-j][k];
            }
            if(!isfinite(smooth) || smooth<0.0)return 0;
            result.amplitude[ix]=smooth;
            for(unsigned j=0u;j<3u;j++)
                next.recent[j][k]=next.recent[j+1u][k];
            next.recent[3u][k]=a;
            stats.active_bins++;
            if(smooth>0.0)stats.nonzero_amplitudes++;
        }
    }
    if(stats.active_bins!=noise_report->vectors)return 0;
    next.frames++;
    next.ready=1u;
    *out=result;
    *report=stats;
    *state=next;
    return 1;
}
