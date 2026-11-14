/* SBR-54g: deterministic complex two-lag LPC coefficients.
 * Production decoder remains untouched; no HE-AAC PCM asserted. */
#include "sauti_aac_sbr_lpc_estimate.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define SLOTS 32u
#define BANDS 32u
#define ENTRIES ((SLOTS+2u)*BANDS)

static SautiAacSbrComplex cmul(SautiAacSbrComplex a,
                               SautiAacSbrComplex b){
    const SautiAacSbrComplex x={
        a.re*b.re-a.im*b.im,a.re*b.im+a.im*b.re
    };
    return x;
}
static void generate(SautiAacSbrComplex x[ENTRIES]){
    memset(x,0,ENTRIES*sizeof(*x));
    const SautiAacSbrComplex a0={-0.55f,0.14f},a1={0.13f,-0.06f};
    x[0*BANDS+7u]=(SautiAacSbrComplex){0.3f,0.19f};
    x[1*BANDS+7u]=(SautiAacSbrComplex){-0.14f,0.37f};
    for(unsigned t=2u;t<SLOTS+2u;t++){
        const SautiAacSbrComplex p=cmul(a0,x[(t-1u)*BANDS+7u]);
        const SautiAacSbrComplex q=cmul(a1,x[(t-2u)*BANDS+7u]);
        x[t*BANDS+7u].re=-p.re-q.re;
        x[t*BANDS+7u].im=-p.im-q.im;
    }
    /* Band 12 contains nonzero excitation, but is still bounded. */
    for(unsigned t=0u;t<SLOTS+2u;t++){
        x[t*BANDS+12u].re=(float)(
            0.2*sin(0.13*(double)t)+0.07*cos(0.371*(double)t));
        x[t*BANDS+12u].im=(float)(
            0.16*cos(0.19*(double)t)-0.04*sin(0.35*(double)t));
    }
}

int main(void){
    SautiAacSbrComplex x[ENTRIES];
    SautiAacSbrLpcCoeffs coeff;
    generate(x);
    memset(&coeff,0xa5,sizeof(coeff));
    assert(sauti_aac_sbr_lpc_estimate_2(x,ENTRIES,SLOTS,&coeff));
    assert(fabs((double)coeff.alpha0[7u].re+0.55)<0.002);
    assert(fabs((double)coeff.alpha0[7u].im-0.14)<0.002);
    assert(fabs((double)coeff.alpha1[7u].re-0.13)<0.002);
    assert(fabs((double)coeff.alpha1[7u].im+0.06)<0.002);
    assert(coeff.alpha0[0].re==0.0f &&
           coeff.alpha0[0].im==0.0f &&
           coeff.alpha1[0].re==0.0f &&
           coeff.alpha1[0].im==0.0f);
    assert(isfinite(coeff.alpha0[12u].re) &&
           isfinite(coeff.alpha1[12u].im));
    puts("SBR-54g: known complex AR(2) coefficients and silent subbands PASS");

    const SautiAacSbrLpcCoeffs original=coeff;
    x[(SLOTS+1u)*BANDS+31u].im=NAN;
    assert(!sauti_aac_sbr_lpc_estimate_2(x,ENTRIES,SLOTS,&coeff));
    assert(memcmp(&original,&coeff,sizeof(coeff))==0);
    x[(SLOTS+1u)*BANDS+31u].im=0.0f;
    assert(!sauti_aac_sbr_lpc_estimate_2(x,ENTRIES-1u,SLOTS,&coeff));
    assert(memcmp(&original,&coeff,sizeof(coeff))==0);
    assert(!sauti_aac_sbr_lpc_estimate_2(x,ENTRIES,0u,&coeff));
    assert(memcmp(&original,&coeff,sizeof(coeff))==0);
    assert(!sauti_aac_sbr_lpc_estimate_2(x,ENTRIES,SLOTS+9u,&coeff));
    assert(memcmp(&original,&coeff,sizeof(coeff))==0);
    puts("SBR-54g: malformed-history / NaN / short buffer rollback PASS");

    /* Every source band silent -> stable, all-zero coefficients. */
    memset(x,0,sizeof(x));
    assert(sauti_aac_sbr_lpc_estimate_2(x,ENTRIES,SLOTS,&coeff));
    for(unsigned k=0u;k<BANDS;k++){
        assert(coeff.alpha0[k].re==0.0f && coeff.alpha0[k].im==0.0f);
        assert(coeff.alpha1[k].re==0.0f && coeff.alpha1[k].im==0.0f);
    }
    puts("PASS SBR-54g native complex LPC(2) estimator (no PCM)");
    return 0;
}
