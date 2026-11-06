#include "sauti_aac_sbr_lpc.h"
#include "sauti_aac_sbr_limits.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define B SAUTI_AAC_SBR_HF_LOW_BANDS
#define H SAUTI_AAC_SBR_HF_HIGH_BANDS
#define W SAUTI_AAC_SBR_LPC_WINDOW
static void approx(float actual,double expected) {
    assert(isfinite(actual) && isfinite(expected));
    assert(fabs((double)actual-expected)<=
           0.000003*fmax(1.0,fabs(expected)));
}
static void test_analytical_golden(void) {
    SautiAacSbrComplex x[W*B]={0};
    SautiAacSbrLpcCoeffs alpha;
    /* x[10]=1, x[11]=1 on band 0:
     * E0=E1=2, one-lag autocorrelation=1, two-lag=0.
     * Determinant=4-1/1.000001; alpha1=1/det,
     * alpha0=-(1+alpha1)/2. */
    x[10u*B].re=1.0f;
    x[11u*B].re=1.0f;
    /* A 90-degree phase rotation on band 1 exercises complex
     * signs, not just real-valued regression coefficients. */
    x[10u*B+1u].re=1.0f;
    x[11u*B+1u].im=1.0f;
    memset(&alpha,0xd9,sizeof(alpha));
    assert(sauti_aac_sbr_lpc_estimate(x,W*B,2u,&alpha));
    const double d=4.0-1.0/1.000001;
    approx(alpha.alpha1[0].re,1.0/d);
    approx(alpha.alpha1[0].im,0.0);
    approx(alpha.alpha0[0].re,-(1.0+1.0/d)/2.0);
    approx(alpha.alpha0[0].im,0.0);
    approx(alpha.alpha1[1].re,-1.0/d);
    approx(alpha.alpha1[1].im,0.0);
    approx(alpha.alpha0[1].re,0.0);
    approx(alpha.alpha0[1].im,-(1.0+1.0/d)/2.0);
    for(unsigned k=2u;k<B;k++) {
        assert(alpha.alpha0[k].re==0.0f &&
               alpha.alpha0[k].im==0.0f &&
               alpha.alpha1[k].re==0.0f &&
               alpha.alpha1[k].im==0.0f);
    }
    /* Relative amplitude scaling must not alter the alpha ratios. */
    for(unsigned t=0;t<W;t++)
        for(unsigned k=0;k<2u;k++) {
            x[t*B+k].re*=1e-20f;
            x[t*B+k].im*=1e-20f;
        }
    SautiAacSbrLpcCoeffs quiet;
    assert(sauti_aac_sbr_lpc_estimate(x,W*B,2u,&quiet));
    approx(quiet.alpha0[0].re,alpha.alpha0[0].re);
    approx(quiet.alpha1[1].re,alpha.alpha1[1].re);
    approx(quiet.alpha0[1].im,alpha.alpha0[1].im);
    for(unsigned t=0;t<W;t++)
        for(unsigned k=0;k<2u;k++) {
            x[t*B+k].re*=1e36f;
            x[t*B+k].im*=1e36f;
        }
    assert(sauti_aac_sbr_lpc_estimate(x,W*B,2u,&quiet));
    approx(quiet.alpha0[0].re,alpha.alpha0[0].re);
    approx(quiet.alpha1[1].re,alpha.alpha1[1].re);
    approx(quiet.alpha0[1].im,alpha.alpha0[1].im);
}

static void test_silence_and_noise(void) {
    SautiAacSbrComplex x[W*B]={0};
    SautiAacSbrLpcCoeffs alpha;
    memset(&alpha,0x91,sizeof(alpha));
    assert(sauti_aac_sbr_lpc_estimate(x,W*B,32u,&alpha));
    for(unsigned k=0;k<B;k++){
        assert(alpha.alpha0[k].re==0.0f && alpha.alpha0[k].im==0.0f);
        assert(alpha.alpha1[k].re==0.0f && alpha.alpha1[k].im==0.0f);
    }
    uint32_t seed=0x1234abcd;
    for(unsigned t=0;t<W;t++)for(unsigned k=0;k<B;k++){
        seed=seed*1664525u+1013904223u;
        x[t*B+k].re=(float)((int)((seed>>13)&1023u)-512)*0.002f;
        seed=seed*1664525u+1013904223u;
        x[t*B+k].im=(float)((int)((seed>>13)&1023u)-512)*0.002f;
    }
    assert(sauti_aac_sbr_lpc_estimate(x,W*B,32u,&alpha));
    unsigned nonzero=0;
    for(unsigned k=0;k<B;k++){
        const float r=alpha.alpha0[k].re, i=alpha.alpha0[k].im;
        const float s=alpha.alpha1[k].re, j=alpha.alpha1[k].im;
        assert(isfinite(r)&&isfinite(i)&&isfinite(s)&&isfinite(j));
        assert((double)r*r+(double)i*i<16.0);
        assert((double)s*s+(double)j*j<16.0);
        if(r || i || s || j)nonzero++;
    }
    assert(nonzero>=24u);
}

static void test_fail_closed(void) {
    SautiAacSbrComplex x[W*B]={0};
    SautiAacSbrLpcCoeffs alpha,old;
    memset(&alpha,0x6b,sizeof(alpha));old=alpha;
#define NOCHANGE(A,L,N,O) do {                                         \
    assert(!sauti_aac_sbr_lpc_estimate((A),(L),(N),(O)));               \
    assert(memcmp(&alpha,&old,sizeof(alpha))==0);                       \
} while(0)
    NOCHANGE(NULL,W*B,32u,&alpha);
    NOCHANGE(x,W*B,0u,&alpha);
    NOCHANGE(x,W*B,33u,&alpha);
    NOCHANGE(x,W*B-1u,32u,&alpha);
    NOCHANGE(x,0u,32u,&alpha);
    assert(!sauti_aac_sbr_lpc_estimate(x,W*B,32u,NULL));
    x[39u*B+31u].re=NAN;
    NOCHANGE(x,W*B,32u,&alpha);
    x[39u*B+31u].re=INFINITY;
    NOCHANGE(x,W*B,32u,&alpha);
    x[39u*B+31u].re=0.0f;
    x[0u*B+0u].im=NAN;
    NOCHANGE(x,W*B,32u,&alpha);
    x[0u*B+0u].im=0.0f;
    /* Unused bands are deliberately not read and can contain NaN. */
    x[39u*B+31u].re=NAN;
    assert(sauti_aac_sbr_lpc_estimate(x,W*B,2u,&alpha));
    assert(alpha.alpha0[31u].re==0.0f);
#undef NOCHANGE
}

static void test_native_chain(void) {
    SautiAacSbrMaster master;
    SautiAacSbrTables t;
    SautiAacSbrPatches patches;
    assert(sauti_aac_sbr_master_linear(16,32,1,0,&master));
    assert(sauti_aac_sbr_tables_build(&master,1,&t));
    assert(sauti_aac_sbr_patch_build(48000,&master,&t,&patches));
    assert(patches.source_for_target[16]==2);
    SautiAacSbrComplex low[W*B]={0};
    low[10u*B+2u].re=1.0f;
    low[11u*B+2u].im=1.0f;
    SautiAacSbrLpcCoeffs coeff;
    assert(sauti_aac_sbr_lpc_estimate(low,W*B,16u,&coeff));
    const double d=4.0-1.0/1.000001;
    approx(coeff.alpha0[2].im,-(1.0+1.0/d)/2.0);
    approx(coeff.alpha1[2].re,-1.0/d);
    SautiAacSbrChirp chirp={0};
    const uint8_t modes[5]={2,2,2,2,2};
    assert(sauti_aac_sbr_chirp_update(modes,t.n_noise,&chirp));
    SautiAacSbrComplex high[38u*H];
    assert(sauti_aac_sbr_hf_predict(
        &patches,&t,low,W*B,38u,&coeff,&chirp,high,38u*H));
    const double bw=chirp.bw[0];
    /* For the source impulse 1,i,0: x12=0, alpha0*i =
     * +(1+1/d)/2, alpha1*1 = -1/d. */
    approx(high[10u*H+16u].re,
           bw*(1.0+1.0/d)/2.0-(bw*bw)/d);
    approx(high[10u*H+16u].im,0.0);
    for(unsigned k=0;k<16u;k++)
        assert(high[10u*H+k].re==0.0f && high[10u*H+k].im==0.0f);
}

int main(void) {
    test_analytical_golden();
    test_silence_and_noise();
    test_fail_closed();
    test_native_chain();
    puts("PASS HE-AAC SBR-14 LPC: analytical real/complex alpha, scaling, stability, rollback, HF chain");
    return 0;
}
