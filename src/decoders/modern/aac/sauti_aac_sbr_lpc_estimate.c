/* SBR-54g / SBR-78: complex 2-lag QMF LPC estimate, ISO 14496-3 (FFmpeg sbr_hf_inverse_filter) formulation. */
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
       slots>SAUTI_AAC_SBR_HF_PRED_MAX_SLOTS ||
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
        /* ISO 14496-3 / FFmpeg sbr_hf_inverse_filter, written for `slots` equations t=2..slots+1
         * (38 with the 8+32 slot window).  x[t] = low[t][k]; p = x[t-1], q = x[t-2].
         *   phi11r = sum |p|^2           phi22r = sum |q|^2
         *   phi11c = sum conj(q) p       phi00c = sum conj(p) x       phi01c = sum conj(q) x
         *   dk     = phi22r*phi11r - |phi11c|^2/1.000001
         *   alpha1 = (phi00c*phi11c - phi01c*phi11r)/dk
         *   alpha0 = -(phi00c + alpha1*conj(phi11c))/phi11r
         * dk == 0 -> alpha1 = 0, phi11r == 0 -> alpha0 = 0, and |alpha|^2 >= 16 zeroes both.
         * The 1/1.000001 term is the standard's own regularization; no diagonal loading. */
        double phi11r=0.0,phi22r=0.0;
        C64 phi11c={0},phi00c={0},phi01c={0};
        for(unsigned t=2u;t<slots+2u;t++){
            const C64 x=to64(low[t*32u+k]);
            const C64 p=to64(low[(t-1u)*32u+k]);
            const C64 q=to64(low[(t-2u)*32u+k]);
            /* conj_mul(a,b) here is a*conj(b), so conj(q)*p is conj_mul(p,q) etc. */
            const C64 c11=conj_mul(p,q),c00=conj_mul(x,p),c01=conj_mul(x,q);
            phi11r+=mag2(p);
            phi22r+=mag2(q);
            phi11c.re+=c11.re;phi11c.im+=c11.im;
            phi00c.re+=c00.re;phi00c.im+=c00.im;
            phi01c.re+=c01.re;phi01c.im+=c01.im;
        }
        if(!isfinite(phi11r)||!isfinite(phi22r)||
           !finite_c(phi11c)||!finite_c(phi00c)||!finite_c(phi01c))
            return 0;
        const double dk=phi22r*phi11r-mag2(phi11c)/1.000001;
        C64 a1={0},a0={0};
        if(dk!=0.0){
            const C64 m0=mul(phi00c,phi11c);
            a1.re=(m0.re-phi01c.re*phi11r)/dk;
            a1.im=(m0.im-phi01c.im*phi11r)/dk;
        }
        if(phi11r!=0.0){
            const C64 t=mul(a1,(C64){phi11c.re,-phi11c.im});
            a0.re=-(phi00c.re+t.re)/phi11r;
            a0.im=-(phi00c.im+t.im)/phi11r;
        }
        if(!finite_c(a0)||!finite_c(a1))return 0;
        if(mag2(a1)>=16.0 || mag2(a0)>=16.0)
            continue;                        /* both stay zero */
        staged.alpha0[k].re=(float)a0.re;
        staged.alpha0[k].im=(float)a0.im;
        staged.alpha1[k].re=(float)a1.re;
        staged.alpha1[k].im=(float)a1.im;
    }
    *out=staged;
    return 1;
}
