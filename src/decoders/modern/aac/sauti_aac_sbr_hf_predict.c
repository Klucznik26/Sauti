/* HE-AAC SBR-13: smoothed inverse-filtering bandwidth factors and the
 * complex second-order predictor application. These implement the
 * mathematical signal-processing steps on PRE-ANALYZED QMF coefficients;
 * adaptive LPC coefficient estimation and actual QMF banks are separate.
 * No allocations, external decoder, or partial output on error. */
#include "sauti_aac_sbr_hf_predict.h"
#include <math.h>
#include <string.h>

int sauti_aac_sbr_chirp_update(const uint8_t *invf_mode,
                               unsigned noise_band_count,
                               SautiAacSbrChirp *state) {
    if(!invf_mode || !state || noise_band_count<1u ||
       noise_band_count>SAUTI_AAC_SBR_MAX_NOISE_BANDS ||
       state->ready>1u ||
       (state->ready && state->count!=noise_band_count))
        return 0;

    if(state->ready) {
        for(unsigned j=0;j<noise_band_count;j++)
            if(state->previous_mode[j]>3u || !isfinite(state->bw[j]) ||
               state->bw[j]<0.0f || state->bw[j]>=1.0f)
                return 0;
    }

    static const float nominal[4]={0.0f,0.75f,0.9f,0.98f};
    SautiAacSbrChirp next;
    memset(&next,0,sizeof(next));
    next.ready=1u;
    next.count=noise_band_count;
    for(unsigned j=0;j<noise_band_count;j++) {
        const unsigned current=invf_mode[j];
        if(current>3u)return 0;
        const unsigned previous=state->ready?state->previous_mode[j]:0u;
        const float old_bw=state->ready?state->bw[j]:0.0f;
        /* Off<->low transition uses the special intermediate level. */
        float target = (current+previous==1u) ?
                       0.6f : nominal[current];
        const float bw = target < old_bw ?
                         0.75f*target+0.25f*old_bw :
                         0.90625f*target+0.09375f*old_bw;
        if(!isfinite(bw) || bw<0.0f || bw>=1.0f)return 0;
        next.bw[j]=bw<0.015625f?0.0f:bw;
        next.previous_mode[j]=(uint8_t)current;
    }
    *state=next;
    return 1;
}

static SautiAacSbrComplex mul(SautiAacSbrComplex a,
                              SautiAacSbrComplex b) {
    SautiAacSbrComplex v;
    v.re=a.re*b.re-a.im*b.im;
    v.im=a.re*b.im+a.im*b.re;
    return v;
}

static int valid_coeff(SautiAacSbrComplex a) {
    if(!isfinite(a.re) || !isfinite(a.im))return 0;
    const double m=(double)a.re*a.re+(double)a.im*a.im;
    return m<16.0;
}

int sauti_aac_sbr_hf_predict(const SautiAacSbrPatches *patches,
                              const SautiAacSbrTables *tables,
                              const SautiAacSbrComplex *low,
                              size_t low_count,
                              unsigned slots,
                              const SautiAacSbrLpcCoeffs *coeffs,
                              const SautiAacSbrChirp *chirp,
                              SautiAacSbrComplex *high,
                              size_t high_count) {
    if(!low || !coeffs || !chirp || !high ||
       slots<1u || slots>SAUTI_AAC_SBR_HF_MAX_SLOTS ||
       low_count<((size_t)slots+2u)*SAUTI_AAC_SBR_HF_LOW_BANDS ||
       high_count<(size_t)slots*SAUTI_AAC_SBR_HF_HIGH_BANDS ||
       !sauti_aac_sbr_hf_map_valid(patches,tables) ||
       chirp->ready!=1u || chirp->count!=tables->n_noise)
        return 0;

    for(unsigned j=0;j<tables->n_noise;j++) {
        if(chirp->previous_mode[j]>3u ||
           !isfinite(chirp->bw[j]) ||
           chirp->bw[j]<0.0f || chirp->bw[j]>=1.0f)
            return 0;
    }

    SautiAacSbrComplex staged[
        SAUTI_AAC_SBR_HF_MAX_SLOTS*SAUTI_AAC_SBR_HF_HIGH_BANDS];
    memset(staged,0,(size_t)slots*SAUTI_AAC_SBR_HF_HIGH_BANDS*
                    sizeof(staged[0]));

    /* Each high frequency bin belongs to exactly one noise frequency
     * interval [noise[g], noise[g+1]) and has one QMF source subband.
     * Source time series carries two preceding slots for the 2nd order
     * predictor. Test finiteness AFTER every operation as an extra
     * guard for large but individually finite inputs. */
    unsigned g=0u;
    for(unsigned k=tables->kx;k<tables->kx+tables->m;k++) {
        while(g+1u<tables->n_noise && k>=tables->noise[g+1u])g++;
        if(k<tables->noise[g] || k>=tables->noise[g+1u])
            return 0;
        const int p=patches->source_for_target[k];
        if(p<0)continue;
        const unsigned src=(unsigned)p;
        const SautiAacSbrComplex a0=coeffs->alpha0[src];
        const SautiAacSbrComplex a1=coeffs->alpha1[src];
        if(!valid_coeff(a0) || !valid_coeff(a1))return 0;
        const float bw=chirp->bw[g];
        const float bw2=bw*bw;
        for(unsigned t=0;t<slots;t++) {
            const SautiAacSbrComplex x0=
                low[((size_t)t+2u)*SAUTI_AAC_SBR_HF_LOW_BANDS+src];
            const SautiAacSbrComplex x1=
                low[((size_t)t+1u)*SAUTI_AAC_SBR_HF_LOW_BANDS+src];
            const SautiAacSbrComplex x2=
                low[(size_t)t*SAUTI_AAC_SBR_HF_LOW_BANDS+src];
            if(!isfinite(x0.re) || !isfinite(x0.im) ||
               !isfinite(x1.re) || !isfinite(x1.im) ||
               !isfinite(x2.re) || !isfinite(x2.im))return 0;
            const SautiAacSbrComplex u0=mul(a0,x1);
            const SautiAacSbrComplex u1=mul(a1,x2);
            SautiAacSbrComplex value;
            value.re=x0.re+bw*u0.re+bw2*u1.re;
            value.im=x0.im+bw*u0.im+bw2*u1.im;
            if(!isfinite(value.re) || !isfinite(value.im))return 0;
            staged[(size_t)t*SAUTI_AAC_SBR_HF_HIGH_BANDS+k]=value;
        }
    }
    memcpy(high,staged,(size_t)slots*SAUTI_AAC_SBR_HF_HIGH_BANDS*
                       sizeof(staged[0]));
    return 1;
}
