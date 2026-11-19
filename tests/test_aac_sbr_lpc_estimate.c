/* SBR-54g: deterministic complex two-lag LPC coefficients.
 * Production decoder remains untouched; no HE-AAC PCM asserted. */
#include "sauti_aac_sbr_lpc_estimate.h"
#include <assert.h>
#include <complex.h>
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

    /* SBR-78: the FFmpeg / ISO window. 8 history slots + 32 current = 40 slots, 38 equations
     * (t = 2..39).  Compared against an independent closed-form solution of the 2x2 normal
     * equations, and against the shorter 32-equation window the probe used before (it must
     * differ on a signal without exact AR structure, otherwise the window would not matter). */
    {
        enum {HIST=40u,EQ=38u};
        static SautiAacSbrComplex w[HIST*BANDS];
        unsigned long long seed=0x9e3779b97f4a7c15ull;
        for(unsigned i=0u;i<HIST*BANDS;i++){
            seed=seed*6364136223846793005ull+1442695040888963407ull;
            const float re=(float)((int)((seed>>40)&0xffffu)-32768)/32768.0f;
            seed=seed*6364136223846793005ull+1442695040888963407ull;
            const float im=(float)((int)((seed>>40)&0xffffu)-32768)/32768.0f;
            w[i].re=re*(0.2f+0.05f*(float)(i%BANDS));
            w[i].im=im*(0.2f+0.05f*(float)(i%BANDS));
        }
        for(unsigned t=0u;t<HIST;t++){          /* band 5: a pure tone, singular without the 1/1.000001 term */
            w[t*BANDS+5u].re=(float)(0.5*cos(0.7*(double)t));
            w[t*BANDS+5u].im=(float)(0.5*sin(0.7*(double)t));
        }
        for(unsigned t=0u;t<HIST;t++){          /* band 9: growth x5 per slot, |alpha|^2 >= 16 before the validity guard */
            const float g=(float)(1e-20*pow(5.0,(double)t));
            w[t*BANDS+9u].re=g;w[t*BANDS+9u].im=0.5f*g;
        }
        SautiAacSbrLpcCoeffs c38,c32;
        assert(sauti_aac_sbr_lpc_estimate_2(w,HIST*BANDS,EQ,&c38));
        assert(sauti_aac_sbr_lpc_estimate_2(w+(HIST-34u)*BANDS,34u*BANDS,32u,&c32));
        double worst=0.0,diff_window=0.0;
        for(unsigned k=0u;k<BANDS;k++){
            double complex spp=0,sqq=0,spq=0,sxp=0,sxq=0;
            for(unsigned t=2u;t<HIST;t++){
                const double complex x=w[t*BANDS+k].re+I*(double)w[t*BANDS+k].im;
                const double complex pv=w[(t-1u)*BANDS+k].re+I*(double)w[(t-1u)*BANDS+k].im;
                const double complex qv=w[(t-2u)*BANDS+k].re+I*(double)w[(t-2u)*BANDS+k].im;
                spp+=conj(pv)*pv;sqq+=conj(qv)*qv;spq+=conj(pv)*qv;
                sxp+=conj(pv)*x;sxq+=conj(qv)*x;
            }
            /* ISO 14496-3 / FFmpeg, in this test's notation (spp=phi11r, sqq=phi22r, spq=conj(phi11c), sxp=phi00c, sxq=phi01c):
             *   dk = sqq*spp - |spq|^2/1.000001;  a1 = (sxp*conj(spq) - sxq*spp)/dk;  a0 = -(sxp + a1*spq)/spp. */
            const double complex det=sqq*spp-spq*conj(spq)/1.000001;
            double complex a0=0,a1=0;
            if(det!=0.0){
                a1=(sxp*conj(spq)-sxq*spp)/det;
                a0=-(sxp+a1*spq)/spp;
            }
            const int guarded=cabs(a0)*cabs(a0)>=16.0 || cabs(a1)*cabs(a1)>=16.0;
            if(k==9u)assert(guarded);        /* the band really exercises the validity guard */
            if(guarded)a0=a1=0;
            const double e0=cabs(a0-(c38.alpha0[k].re+I*(double)c38.alpha0[k].im));
            const double e1=cabs(a1-(c38.alpha1[k].re+I*(double)c38.alpha1[k].im));
            if(e0>worst)worst=e0;
            if(e1>worst)worst=e1;
            const double d=cabs((double)c38.alpha0[k].re-(double)c32.alpha0[k].re+
                                I*((double)c38.alpha0[k].im-(double)c32.alpha0[k].im));
            if(d>diff_window)diff_window=d;
        }
        assert(worst<1e-5);          /* independent closed form incl. the standard's regularization */
        assert(fabs((double)c38.alpha0[5u].re)+fabs((double)c38.alpha0[5u].im)>0.0);   /* the tone band is not silently zeroed */
        assert(diff_window>1e-3);    /* the 38-equation window is not the old 32-equation one */
        printf("SBR-78: 38-equation FFmpeg window vs closed form, worst |dalpha|=%.2e; window matters (max |alpha0 38-32|=%.3f) PASS\n",worst,diff_window);
    }
    puts("PASS SBR-54g native complex LPC(2) estimator (no PCM)");
    return 0;
}
