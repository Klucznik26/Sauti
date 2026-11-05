/* SBR-54g: native, regularized, complex 2-lag QMF LPC estimate.
 * Diagnostic / experimental; no normative HE-AAC audio asserted. */
#include "sauti_aac_sbr_lpc_estimate.h"
#include <float.h>
#include <math.h>
#include <string.h>

typedef struct {double re,im;} C64;
static C64 mul(C64 x,C64 y){
    const C64 v={x.re*y.re-x.im*y.im,
                 x.re*y.im+x.im*y.re};return v;
}
static C64 conj_mul(C64 x,C64 y){
    const C64 v={x.re*y.re+x.im*y.im,
                 x.im*y.re-x.re*y.im};return v;
}
static C64 to64(SautiAacSbrComplex x){
    const C64 v={(double)x.re,(double)x.im};return v;
}
static double mag2(C64 x){return x.re*x.re+x.im*x.im;}
static int finite_c(C64 x){return isfinite(x.re)&&isfinite(x.im);}

int sauti_aac_sbr_lpc_estimate_2(
    const SautiAacSbrComplex *low,size_t count,
    unsigned slots,SautiAacSbrLpcCoeffs *out){
    if(!low || !out || slots<2u ||
       slots>SAUTI_AAC_SBR_HF_MAX_SLOTS ||
       count<((size_t)slots+2u)*SAUTI_AAC_SBR_HF_LOW_BANDS)
        return 0;
    for(unsigned t=0u;t<slots+2u;t++)
        for(unsigned k=0u;k<SAUTI_AAC_SBR_HF_LOW_BANDS;k++){
            const SautiAacSbrComplex v=
                low[(size_t)t*SAUTI_AAC_SBR_HF_LOW_BANDS+k];
            if(!isfinite(v.re)||!isfinite(v.im))return 0;
        }
    SautiAacSbrLpcCoeffs staged={0};
    for(unsigned k=0u;k<SAUTI_AAC_SBR_HF_LOW_BANDS;k++){
        double r00=0.0,r11=0.0;
        C64 r01={0},b0={0},b1={0};
        for(unsigned t=2u;t<slots+2u;t++){
            const C64 x=to64(low[t*32u+k]);
            const C64 p=to64(low[(t-1u)*32u+k]);
            const C64 q=to64(low[(t-2u)*32u+k]);
            const C64 rpq=conj_mul(q,p); /* X^H X: conj(previous1)*previous2 */
            const C64 bx=conj_mul(x,p),by=conj_mul(x,q);
            r00+=mag2(p);
            r11+=mag2(q);
            r01.re+=rpq.re;r01.im+=rpq.im;
            b0.re-=bx.re;b0.im-=bx.im;
            b1.re-=by.re;b1.im-=by.im;
        }
        if(!isfinite(r00)||!isfinite(r11)||
           !finite_c(r01)||!finite_c(b0)||!finite_c(b1))
            return 0;
        if(r00+r11<=1e-28)
            continue; /* effectively silent; zero predictor */
        /* Diagonal loading gives a positive definite complex
         * Hermitian covariance even with near-tonal dependencies.
         * Never invent a coefficient based on an unstable inverse. */
        const double ridge=1e-7*(r00+r11)+1e-20;
        r00+=ridge;r11+=ridge;
        const double det=r00*r11-mag2(r01);
        if(!isfinite(det)||det<=0.0)return 0;
        const C64 p0=mul(r01,b1);
        const C64 c0={(b0.re*r11-p0.re)/det,
                      (b0.im*r11-p0.im)/det};
        const C64 r10={r01.re,-r01.im};
        const C64 p1=mul(r10,b0);
        const C64 c1={(r00*b1.re-p1.re)/det,
                      (r00*b1.im-p1.im)/det};
        if(!finite_c(c0)||!finite_c(c1))return 0;
        /* ISO predictor validity guard; 54g never publishes
         * unbounded LPC terms into the later SBR-13 stage. */
        if(mag2(c0)>=16.0 || mag2(c1)>=16.0)
            continue;
        if(fabs(c0.re)>FLT_MAX || fabs(c0.im)>FLT_MAX ||
           fabs(c1.re)>FLT_MAX || fabs(c1.im)>FLT_MAX)
            return 0;
        staged.alpha0[k].re=(float)c0.re;
        staged.alpha0[k].im=(float)c0.im;
        staged.alpha1[k].re=(float)c1.re;
        staged.alpha1[k].im=(float)c1.im;
    }
    *out=staged;
    return 1;
}
