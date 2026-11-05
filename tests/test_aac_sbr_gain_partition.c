/* SBR-54j: source-verified pre-limiter energy allocation.
 * Reference equations: FFmpeg libavcodec/aacsbr.c:sbr_gain_calc().
 * No QMF, no output PCM, no production decoder mutation. */
#include "sauti_aac_sbr_gain_partition.h"
#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void eq(double actual,double expected){
    assert(isfinite(actual));
    assert(fabs(actual-expected)<=1e-11*fmax(1.0,fabs(expected)));
}

int main(void){
    SautiAacSbrGainPartition out={0};
    const double noise=sqrt(75.0);

    /* E=100, E_curr=24, Q=3:
     * off-tonal steady: gain=sqrt(100/(25*4))=1,
     * noise sqrt(75), sine=0. */
    assert(sauti_aac_sbr_gain_partition(
        100.0,24.0,3.0,0u,1u,&out));
    eq(out.hf_gain,1.0);
    eq(out.noise_amplitude,noise);
    eq(out.sine_amplitude,0.0);
    puts("SBR-54j: non-tonal steady full Q denominator PASS");

    /* Envelope at the transient: delta=0, noise is still
     * calculated before the stage that may gate its addition. */
    assert(sauti_aac_sbr_gain_partition(
        100.0,24.0,3.0,0u,0u,&out));
    eq(out.hf_gain,2.0);
    eq(out.noise_amplitude,noise);
    eq(out.sine_amplitude,0.0);
    puts("SBR-54j: non-tonal transient delta=0 PASS");

    /* Tone active: harmonic HF gain uses E*Q/((1+Ec)*(1+Q)).
     * Sine level is sqrt(E/(1+Q))=5, independent of delta. */
    for(unsigned delta=0u;delta<=1u;delta++){
        assert(sauti_aac_sbr_gain_partition(
            100.0,24.0,3.0,1u,delta,&out));
        eq(out.hf_gain,sqrt(3.0));
        eq(out.noise_amplitude,noise);
        eq(out.sine_amplitude,5.0);
    }
    puts("SBR-54j: tonal harmonic gain and sine before limiter PASS");

    assert(sauti_aac_sbr_gain_partition(
        100.0,0.0,0.0,0u,1u,&out));
    eq(out.hf_gain,10.0);
    eq(out.noise_amplitude,0.0);
    eq(out.sine_amplitude,0.0);
    assert(sauti_aac_sbr_gain_partition(
        100.0,0.0,0.0,1u,1u,&out));
    eq(out.hf_gain,0.0);
    eq(out.sine_amplitude,10.0);
    assert(sauti_aac_sbr_gain_partition(
        0.0,24.0,3.0,1u,1u,&out));
    eq(out.hf_gain,0.0);
    eq(out.noise_amplitude,0.0);
    eq(out.sine_amplitude,0.0);

    /* Reject invalid values atomically. */
    out=(SautiAacSbrGainPartition){3.0,5.0,7.0};
    const SautiAacSbrGainPartition original=out;
    assert(!sauti_aac_sbr_gain_partition(
        NAN,1.0,2.0,0u,1u,&out));
    assert(!sauti_aac_sbr_gain_partition(
        1.0,-1.0,2.0,0u,1u,&out));
    assert(!sauti_aac_sbr_gain_partition(
        1.0,1.0,INFINITY,0u,1u,&out));
    assert(!sauti_aac_sbr_gain_partition(
        1.0,1.0,2.0,2u,1u,&out));
    assert(!sauti_aac_sbr_gain_partition(
        1.0,1.0,2.0,0u,2u,&out));
    assert(memcmp(&out,&original,sizeof(out))==0);

    /* Very large legal input must not overflow in E*Q. */
    assert(sauti_aac_sbr_gain_partition(
        DBL_MAX,DBL_MAX,DBL_MAX,1u,1u,&out));
    assert(isfinite(out.hf_gain) && isfinite(out.noise_amplitude) &&
           isfinite(out.sine_amplitude));
    puts("SBR-54j: bounded big inputs and atomic rejection PASS");
    puts("PASS SBR-54j normative pre-limiter partition MATH (not PCM)");
    return 0;
}
