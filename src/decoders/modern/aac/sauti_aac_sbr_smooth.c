/* HE-AAC SBR-21: the MPEG-4 SBR time smoothing kernel, applied to
 * per-subband real gain and noise-amplitude coefficients. The DSP
 * signal and oscillator stages must consume the smoothed values later.
 * Never smooth complex QMF samples as a substitute. Independent,
 * allocation-free code; all outputs and history update atomically.
 */
#include "sauti_aac_sbr_smooth.h"
#include <math.h>
#include <string.h>

static int valid_tables(const SautiAacSbrTables *t) {
    if(!t || t->kx<1u || t->kx>32u || t->m<1u || t->m>64u-t->kx ||
       t->n_high<1u || t->n_high>SAUTI_AAC_SBR_MAX_MASTER_BANDS ||
       t->n_low!=(t->n_high+1u)/2u ||
       t->n_noise<1u || t->n_noise>SAUTI_AAC_SBR_MAX_NOISE_BANDS ||
       t->n_noise>t->n_low ||
       t->high[0]!=t->kx || t->high[t->n_high]!=t->kx+t->m ||
       t->low[0]!=t->kx || t->low[t->n_low]!=t->kx+t->m ||
       t->noise[0]!=t->kx || t->noise[t->n_noise]!=t->kx+t->m)
        return 0;
    for(unsigned j=1u;j<=t->n_high;j++)
        if(t->high[j]<=t->high[j-1u])return 0;
    const unsigned odd=t->n_high&1u;
    for(unsigned j=0u;j<=t->n_low;j++) {
        const unsigned ix=j?2u*j-odd:0u;
        if(ix>t->n_high || t->low[j]!=t->high[ix])return 0;
    }
    for(unsigned j=1u;j<=t->n_noise;j++){
        if(t->noise[j]<=t->noise[j-1u])return 0;
        int found=0;
        for(unsigned k=1u;k<=t->n_low;k++)
            if(t->low[k]==t->noise[j])found=1;
        if(!found)return 0;
    }
    return 1;
}

int sauti_aac_sbr_smooth_coefficients(
    const SautiAacSbrTables *tables,
    unsigned env_count,
    const uint8_t *env_edges,
    unsigned slots,
    const double (*target_gain)[SAUTI_AAC_SBR_SMOOTH_BANDS],
    const double (*target_noise)[SAUTI_AAC_SBR_SMOOTH_BANDS],
    const uint8_t *attack,
    unsigned bs_smoothing_mode,
    SautiAacSbrSmoothState *state,
    SautiAacSbrSmoothFrame *out) {
    if(!valid_tables(tables) || !env_edges || !target_gain ||
       !target_noise || !attack || !state || !out ||
       env_count<1u || env_count>SAUTI_AAC_SBR_MAX_ENV ||
       slots<1u || slots>SAUTI_AAC_SBR_SMOOTH_SLOTS ||
       bs_smoothing_mode>1u ||
       env_edges[0]!=0u || env_edges[env_count]!=slots ||
       state->ready>1u ||
       (state->ready &&
        (state->kx!=tables->kx || state->m!=tables->m)))
        return 0;

    const unsigned begin=tables->kx, end=tables->kx+tables->m;
    for(unsigned e=0u;e<env_count;e++) {
        if(env_edges[e]>=env_edges[e+1u] ||
           env_edges[e+1u]>slots || attack[e]>1u)return 0;
        for(unsigned k=begin;k<end;k++) {
            if(!isfinite(target_gain[e][k]) ||
               !isfinite(target_noise[e][k]) ||
               target_gain[e][k]<0.0 ||
               target_noise[e][k]<0.0)
                return 0;
        }
    }
    if(state->ready)
        for(unsigned j=0u;j<SAUTI_AAC_SBR_SMOOTH_HISTORY;j++)
            for(unsigned k=begin;k<end;k++)
                if(!isfinite(state->gain[j][k]) ||
                   !isfinite(state->noise[j][k]) ||
                   state->gain[j][k]<0.0 ||
                   state->noise[j][k]<0.0)return 0;

    /* h[0] multiplies the CURRENT gain/noise target; h[1..4]
     * multiply the four preceding QMF slots, newest first. */
    static const double h[5]={
        0.33333333333333,0.30150283239582,
        0.21816949906249,0.11516383427084,0.03183050093751
    };

    SautiAacSbrSmoothFrame result;
    SautiAacSbrSmoothState next=*state;
    memset(&result,0,sizeof(result));
    if(!next.ready) {
        memset(&next,0,sizeof(next));
        next.ready=1u;
        next.kx=tables->kx;
        next.m=tables->m;
        for(unsigned j=0u;j<SAUTI_AAC_SBR_SMOOTH_HISTORY;j++)
            for(unsigned k=begin;k<end;k++) {
                next.gain[j][k]=target_gain[0][k];
                next.noise[j][k]=target_noise[0][k];
            }
    }

    unsigned e=0u;
    for(unsigned t=0u;t<slots;t++){
        while(e+1u<env_count && t>=env_edges[e+1u])e++;
        const int do_smooth=bs_smoothing_mode==0u && !attack[e];
        for(unsigned k=begin;k<end;k++){
            const double g=target_gain[e][k],n=target_noise[e][k];
            double sg=g,sn=n;
            if(do_smooth) {
                sg=h[0]*g; sn=h[0]*n;
                for(unsigned j=0u;j<SAUTI_AAC_SBR_SMOOTH_HISTORY;j++){
                    const unsigned older=SAUTI_AAC_SBR_SMOOTH_HISTORY-1u-j;
                    sg+=h[j+1u]*next.gain[older][k];
                    sn+=h[j+1u]*next.noise[older][k];
                }
            }
            if(!isfinite(sg) || !isfinite(sn) || sg<0.0 || sn<0.0)
                return 0;
            result.gain[t][k]=sg;
            result.noise[t][k]=sn;
            for(unsigned j=0u;j+1u<SAUTI_AAC_SBR_SMOOTH_HISTORY;j++){
                next.gain[j][k]=next.gain[j+1u][k];
                next.noise[j][k]=next.noise[j+1u][k];
            }
            next.gain[SAUTI_AAC_SBR_SMOOTH_HISTORY-1u][k]=g;
            next.noise[SAUTI_AAC_SBR_SMOOTH_HISTORY-1u][k]=n;
        }
    }
    *out=result;
    *state=next;
    return 1;
}
