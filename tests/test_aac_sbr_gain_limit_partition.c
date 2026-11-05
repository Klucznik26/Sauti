/* SBR-54k: numeric end-to-end FFmpeg-style group limiter
 * on the SBR-54j HF/noise/sine pre-limiter partition.
 * No QMF/PCM and no decoder path modification. */
#include "sauti_aac_sbr_gain_limit_partition.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void eq(double a,double b){
    assert(isfinite(a)&&isfinite(b));
    assert(fabs(a-b)<2e-11*fmax(1.0,fabs(b)));
}
int main(void){
    double e[64]={0},ec[64]={0},q[64]={0};
    uint8_t sine[64]={0},edges[65]={0};
    SautiAacSbrGainLimitedFrame result={0};
    edges[0]=16u;edges[1]=17u;edges[2]=18u;
    e[16]=100.0;ec[16]=24.0;q[16]=3.0;
    e[17]=100.0;ec[17]=24.0;q[17]=3.0;sine[17]=1u;
    assert(sauti_aac_sbr_gain_limit_partition(
        e,ec,q,sine,edges,2u,1u,1u,&result));
    /* Group 0: G=1, Q=sqrt(75), after=24+75=99.
     * Group 1: G=sqrt(3), S=5, after=24*3+25=97.
     * Noise is absent from energy sum when S>0. */
    const double boost0=sqrt((100.0+0x1p-23)/(99.0+0x1p-23));
    const double boost1=sqrt((100.0+0x1p-23)/(97.0+0x1p-23));
    eq(result.group_cap[0],sqrt(100.0/24.0));
    eq(result.group_cap[1],sqrt(100.0/24.0));
    eq(result.group_boost[0],boost0);
    eq(result.group_boost[1],boost1);
    eq(result.band[16].hf_gain,boost0);
    eq(result.band[16].noise_amplitude,sqrt(75.0)*boost0);
    eq(result.band[16].sine_amplitude,0.0);
    eq(result.band[17].hf_gain,sqrt(3.0)*boost1);
    eq(result.band[17].noise_amplitude,sqrt(75.0)*boost1);
    eq(result.band[17].sine_amplitude,5.0*boost1);
    assert(result.sine_bins==1u &&
           result.noise_capped_bins==0u &&
           result.gain_capped_bins==0u);
    puts("SBR-54k: independent two-group tonal/non-tonal boost PASS");

    /* An attack envelope delta=0 excludes Q energy from boost,
     * while its HF gain differs from the non-attack case. */
    assert(sauti_aac_sbr_gain_limit_partition(
        e,ec,q,sine,edges,2u,1u,0u,&result));
    eq(result.band[16].hf_gain,
       2.0*sqrt((100.0+0x1p-23)/(96.0+0x1p-23)));
    eq(result.band[17].hf_gain,sqrt(3.0)*boost1);
    puts("SBR-54k: attack-aware energy budget PASS");

    /* Force gain cap and noise cap in a shared limiter group.
     * Large E_curr in bin 17 drives cap low while a non-tonal
     * bin 16 originally has G=1, Q=sqrt(75). */
    edges[0]=16u;edges[1]=18u;
    e[17]=1.0;ec[17]=1000000.0;q[17]=0.0;sine[17]=0u;
    assert(sauti_aac_sbr_gain_limit_partition(
        e,ec,q,sine,edges,1u,0u,1u,&result));
    const double cap=0.70795*
        sqrt((101.0+0x1p-23)/(1000024.0+0x1p-23));
    eq(result.group_cap[0],cap);
    eq(result.group_boost[0],1.584893192);
    eq(result.band[16].hf_gain,cap*1.584893192);
    eq(result.band[16].noise_amplitude,
       sqrt(75.0)*cap*1.584893192);
    eq(result.band[17].hf_gain,
       sqrt(1.0/1000001.0)*1.584893192);
    assert(result.gain_capped_bins==1u &&
           result.noise_capped_bins==1u &&
           result.sine_bins==0u);
    puts("SBR-54k: limiter caps HF+noise then common maximum boost PASS");

    /* Out-of-band slots do not contribute; invalid geometry and a
     * late NaN never publish partial results or altered counters. */
    const SautiAacSbrGainLimitedFrame old=result;
    e[17]=NAN;
    assert(!sauti_aac_sbr_gain_limit_partition(
        e,ec,q,sine,edges,1u,0u,1u,&result));
    assert(memcmp(&old,&result,sizeof(old))==0);
    e[17]=1.0;
    edges[1]=16u;
    assert(!sauti_aac_sbr_gain_limit_partition(
        e,ec,q,sine,edges,1u,0u,1u,&result));
    assert(memcmp(&old,&result,sizeof(old))==0);
    edges[1]=18u;
    assert(!sauti_aac_sbr_gain_limit_partition(
        e,ec,q,sine,edges,1u,4u,1u,&result));
    assert(memcmp(&old,&result,sizeof(old))==0);
    q[17]=-1.0;
    assert(!sauti_aac_sbr_gain_limit_partition(
        e,ec,q,sine,edges,1u,0u,1u,&result));
    assert(memcmp(&old,&result,sizeof(old))==0);
    q[17]=0.0;
    sine[17]=2u;
    assert(!sauti_aac_sbr_gain_limit_partition(
        e,ec,q,sine,edges,1u,0u,1u,&result));
    assert(memcmp(&old,&result,sizeof(old))==0);
    puts("SBR-54k: NaN, invalid edge, mode, Q and sine rollback PASS");
    puts("PASS SBR-54k reference HF/noise/sine limiter math (no PCM)");
    return 0;
}
