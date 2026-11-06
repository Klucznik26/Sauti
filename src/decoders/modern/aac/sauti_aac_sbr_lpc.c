/* SBR-14: native 2nd-order complex LPC coefficients from 40 QMF slots.
 * The correlation window and two-lag Yule-Walker equations follow the
 * classic MPEG-4 AAC SBR high-frequency generator. Implemented here
 * independently; no FFmpeg/FDK runtime dependency or heap allocation.
 * Double intermediates and per-band peak normalization avoid overflow.
 * Both alpha vectors are published atomically. */
#include "sauti_aac_sbr_lpc.h"
#include <math.h>
#include <string.h>

int sauti_aac_sbr_lpc_estimate(const SautiAacSbrComplex *low,
                               size_t low_count,
                               unsigned bands,
                               SautiAacSbrLpcCoeffs *out) {
    if (!low || !out || bands < 1u ||
        bands > SAUTI_AAC_SBR_HF_LOW_BANDS ||
        low_count < (size_t)SAUTI_AAC_SBR_LPC_WINDOW *
                    SAUTI_AAC_SBR_HF_LOW_BANDS)
        return 0;

    SautiAacSbrLpcCoeffs next;
    memset(&next,0,sizeof(next));
    for (unsigned band=0; band<bands; ++band) {
        double scale=0.0;
        for (unsigned t=0;t<SAUTI_AAC_SBR_LPC_WINDOW;++t) {
            const SautiAacSbrComplex x=
                low[(size_t)t*SAUTI_AAC_SBR_HF_LOW_BANDS+band];
            if(!isfinite(x.re) || !isfinite(x.im))return 0;
            const double a=fmax(fabs((double)x.re),fabs((double)x.im));
            if(a>scale)scale=a;
        }
        if(scale==0.0)continue; /* silence -> no prediction */

        double xr[SAUTI_AAC_SBR_LPC_WINDOW];
        double xi[SAUTI_AAC_SBR_LPC_WINDOW];
        for(unsigned t=0;t<SAUTI_AAC_SBR_LPC_WINDOW;t++){
            const SautiAacSbrComplex x=
                low[(size_t)t*SAUTI_AAC_SBR_HF_LOW_BANDS+band];
            xr[t]=(double)x.re/scale;
            xi[t]=(double)x.im/scale;
        }

        /* ISO-style correlations, conjugate on the older sample:
         * phi11 = sum(t=0..38) conj(X[t]) * X[t+1]
         * phi00 = sum(t=1..38) conj(X[t]) * X[t+1]
         * phi01 = sum(t=0..37) conj(X[t]) * X[t+2]
         * phi10 = sum(t=1..38) |X[t]|^2
         * phi21 = sum(t=0..37) |X[t]|^2 */
        double p11r=0,p11i=0,p00r=0,p00i=0,p01r=0,p01i=0;
        double p10=0,p21=0;
        for(unsigned t=0;t<SAUTI_AAC_SBR_LPC_WINDOW;t++){
            if(t<38u)
                p21+=xr[t]*xr[t]+xi[t]*xi[t];
            if(t>=1u && t<=38u)
                p10+=xr[t]*xr[t]+xi[t]*xi[t];
            if(t<39u){
                const double re=xr[t]*xr[t+1u]+xi[t]*xi[t+1u];
                const double im=xr[t]*xi[t+1u]-xi[t]*xr[t+1u];
                p11r+=re;p11i+=im;
                if(t>0u){p00r+=re;p00i+=im;}
            }
            if(t<38u){
                p01r+=xr[t]*xr[t+2u]+xi[t]*xi[t+2u];
                p01i+=xr[t]*xi[t+2u]-xi[t]*xr[t+2u];
            }
        }

        /* Regularization 1.000001 removes the exact-singularity path
         * in the usual SBR determinant. A vanishing or ill-conditioned
         * denominator safely produces coefficients of zero. */
        const double det=p21*p10-
            (p11r*p11r+p11i*p11i)/1.000001;
        const double denom_scale=p21*p10+p11r*p11r+p11i*p11i;
        if(p10<=0.0 || !isfinite(det) ||
           fabs(det)<=1e-12*fmax(denom_scale,1e-30))
            continue;

        const double a1r=(p00r*p11r-p00i*p11i-p01r*p10)/det;
        const double a1i=(p00r*p11i+p00i*p11r-p01i*p10)/det;
        const double a0r=-(p00r+a1r*p11r+a1i*p11i)/p10;
        const double a0i=-(p00i+a1i*p11r-a1r*p11i)/p10;
        const double norm0=a0r*a0r+a0i*a0i;
        const double norm1=a1r*a1r+a1i*a1i;
        if(!isfinite(norm0) || !isfinite(norm1) ||
           norm0>=16.0 || norm1>=16.0)
            continue; /* SBR spectral predictor stability limit */
        const SautiAacSbrComplex c0={(float)a0r,(float)a0i};
        const SautiAacSbrComplex c1={(float)a1r,(float)a1i};
        if(!isfinite(c0.re) || !isfinite(c0.im) ||
           !isfinite(c1.re) || !isfinite(c1.im))
            continue;
        next.alpha0[band]=c0;
        next.alpha1[band]=c1;
    }
    *out=next;
    return 1;
}
