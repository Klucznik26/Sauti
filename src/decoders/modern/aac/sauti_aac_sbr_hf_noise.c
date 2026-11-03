/* HE-AAC SBR-18: independently generated complex noise in high QMF.
 * Mean-power partition (signal/noise) follows the linear SBR E and Q
 * scales. The random-phase source is an original, deterministic 8-phase
 * white sequence, NOT the normative 512-entry SBR ROM. Complex QMF
 * data must already have been produced and envelope-adjusted upstream.
 * No heap, external decoder, smoothing, sinusoid or PCM synthesis.
 */
#include "sauti_aac_sbr_hf_noise.h"
#include <float.h>
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
    for(unsigned i=1u;i<=t->n_high;i++)
        if(t->high[i]<=t->high[i-1u])return 0;
    const unsigned odd=t->n_high&1u;
    for(unsigned i=0u;i<=t->n_low;i++){
        const unsigned j=i?2u*i-odd:0u;
        if(j>t->n_high || t->low[i]!=t->high[j])return 0;
    }
    for(unsigned i=1u;i<=t->n_noise;i++){
        if(t->noise[i]<=t->noise[i-1u])return 0;
        int ok=0;
        for(unsigned j=1u;j<=t->n_low;j++)
            if(t->noise[i]==t->low[j])ok=1;
        if(!ok)return 0;
    }
    return 1;
}

static uint32_t next_random(uint32_t *seed) {
    /* Nonzero xorshift32 state has period 2^32-1. */
    uint32_t x=*seed;
    x^=x<<13u;
    x^=x>>17u;
    x^=x<<5u;
    *seed=x;
    return x;
}

static void unit_phase(unsigned phase,double *re,double *im) {
    /* An 8-PSK constellation: magnitude=1, mean=0, and circular
     * symmetry. The exact ISO 512-entry noise vector comes later. */
    static const double real[8]={
        1.0,0.7071067811865475244,0.0,-0.7071067811865475244,
        -1.0,-0.7071067811865475244,0.0,0.7071067811865475244
    };
    static const double imag[8]={
        0.0,0.7071067811865475244,1.0,0.7071067811865475244,
        0.0,-0.7071067811865475244,-1.0,-0.7071067811865475244
    };
    *re=real[phase&7u];*im=imag[phase&7u];
}

static int mix_internal(
    const SautiAacSbrTables *tables,
    const SautiAacSbrEnergyFrame *energy,
    const uint8_t *env_edges,
    const uint8_t *noise_edges,
    unsigned slots,
    const uint8_t *tone_mask,
    size_t tone_count,
    const SautiAacSbrComplex *input,
    size_t input_count,
    SautiAacSbrComplex *output,
    size_t output_count,
    SautiAacSbrNoiseState *state) {
    if(!energy || !env_edges || !noise_edges || !input || !output ||
       !state || !valid_tables(tables) ||
       slots<1u || slots>SAUTI_AAC_SBR_HF_MAX_SLOTS ||
       input_count<(size_t)slots*SAUTI_AAC_SBR_HF_HIGH_BANDS ||
       output_count<(size_t)slots*SAUTI_AAC_SBR_HF_HIGH_BANDS ||
       energy->env_count<1u || energy->env_count>SAUTI_AAC_SBR_MAX_ENV ||
       energy->noise_count!=(energy->env_count==1u?1u:2u) ||
       env_edges[0]!=0u || noise_edges[0]!=0u ||
       env_edges[energy->env_count]!=slots ||
       noise_edges[energy->noise_count]!=slots ||
       state->ready>1u || (state->ready && !state->seed) ||
       (!state->ready && (state->seed || state->emitted)) ||
       state->emitted>UINT64_MAX-(uint64_t)slots*tables->m ||
       (tone_mask &&
        tone_count<(size_t)slots*SAUTI_AAC_SBR_HF_HIGH_BANDS))
        return 0;

    if(tone_mask)
        for(size_t i=0;i<(size_t)slots*SAUTI_AAC_SBR_HF_HIGH_BANDS;i++)
            if(tone_mask[i]>1u)return 0;

    for(unsigned e=0u;e<energy->env_count;e++){
        if(env_edges[e]>=env_edges[e+1u] ||
           env_edges[e+1u]>slots || energy->freq_res[e]>1u)return 0;
        const unsigned n=energy->freq_res[e]?
                         tables->n_high:tables->n_low;
        for(unsigned b=0u;b<n;b++){
            const double v=energy->env_energy[e][b];
            if(!isfinite(v) || v<0.0)return 0;
        }
    }
    for(unsigned n=0u;n<energy->noise_count;n++){
        if(noise_edges[n]>=noise_edges[n+1u] ||
           noise_edges[n+1u]>slots)return 0;
        for(unsigned b=0u;b<tables->n_noise;b++){
            const double q=energy->noise_factor[n][b];
            if(!isfinite(q) || q<0.0)return 0;
        }
    }
    /* In classic SBR the noise grid is a coarser partition of the
     * envelope grid; the single interior noise border must coincide
     * with one of the available envelope borders. */
    for(unsigned n=1u;n<energy->noise_count;n++){
        int found=0;
        for(unsigned e=1u;e<energy->env_count;e++)
            if(noise_edges[n]==env_edges[e])found=1;
        if(!found)return 0;
    }

    /* Per-envelope target power maps, independent of time noise grid. */
    double targets[SAUTI_AAC_SBR_MAX_ENV][64]={0};
    for(unsigned e=0u;e<energy->env_count;e++){
        const uint8_t *freq=energy->freq_res[e]?
                            tables->high:tables->low;
        const unsigned n=energy->freq_res[e]?
                         tables->n_high:tables->n_low;
        for(unsigned b=0u;b<n;b++)
            for(unsigned k=freq[b];k<freq[b+1u];k++)
                targets[e][k]=energy->env_energy[e][b];

        for(unsigned k=tables->kx;k<tables->kx+tables->m;k++){
            double sum=0.0;
            for(unsigned t=env_edges[e];t<env_edges[e+1u];t++){
                const SautiAacSbrComplex v=
                    input[(size_t)t*SAUTI_AAC_SBR_HF_HIGH_BANDS+k];
                if(!isfinite(v.re) || !isfinite(v.im))return 0;
                sum+=(double)v.re*v.re+(double)v.im*v.im;
                if(!isfinite(sum))return 0;
            }
            /* Do not report a 'balanced' noise/signal energy split
             * if the pre-existing HF signal is entirely absent. */
            if(targets[e][k]>0.0 && sum==0.0)return 0;
        }
    }

    SautiAacSbrComplex next[
        SAUTI_AAC_SBR_HF_MAX_SLOTS*SAUTI_AAC_SBR_HF_HIGH_BANDS];
    const size_t count=(size_t)slots*SAUTI_AAC_SBR_HF_HIGH_BANDS;
    memcpy(next,input,count*sizeof(next[0]));

    SautiAacSbrNoiseState updated=*state;
    if(!updated.ready){
        updated.ready=1u;
        updated.seed=0x6a09e667u; /* fixed documented default seed */
    }
    unsigned env=0u,noise_env=0u;
    const unsigned kx=tables->kx,k2=tables->kx+tables->m;
    for(unsigned t=0u;t<slots;t++){
        while(env+1u<energy->env_count && t>=env_edges[env+1u])env++;
        while(noise_env+1u<energy->noise_count &&
              t>=noise_edges[noise_env+1u])noise_env++;
        unsigned noise_band=0u;
        for(unsigned k=kx;k<k2;k++){
            while(noise_band+1u<tables->n_noise &&
                  k>=tables->noise[noise_band+1u])noise_band++;
            const double target=targets[env][k];
            const double q=energy->noise_factor[noise_env][noise_band];
            const size_t ix=(size_t)t*SAUTI_AAC_SBR_HF_HIGH_BANDS+k;
            const int tonal=tone_mask && tone_mask[ix];
            const double signal_gain=target==0.0?0.0:
                  (tonal?sqrt(q/(1.0+q)):1.0/sqrt(1.0+q));
            const double noise_amp=tonal?0.0:
                                    sqrt(target*(q/(1.0+q)));
            const uint32_t r=next_random(&updated.seed);
            double nre=0.0,nim=0.0;
            unit_phase(r&7u,&nre,&nim);
            const double re=(double)input[ix].re*signal_gain+
                            noise_amp*nre;
            const double im=(double)input[ix].im*signal_gain+
                            noise_amp*nim;
            if(!isfinite(re) || !isfinite(im) ||
               fabs(re)>FLT_MAX || fabs(im)>FLT_MAX)return 0;
            next[ix].re=(float)re;
            next[ix].im=(float)im;
        }
    }
    updated.emitted+=(uint64_t)slots*tables->m;
    memcpy(output,next,count*sizeof(next[0]));
    *state=updated;
    return 1;
}


int sauti_aac_sbr_hf_noise_mix(
    const SautiAacSbrTables *tables,
    const SautiAacSbrEnergyFrame *energy,
    const uint8_t *env_edges,
    const uint8_t *noise_edges,
    unsigned slots,
    const SautiAacSbrComplex *input,
    size_t input_count,
    SautiAacSbrComplex *output,
    size_t output_count,
    SautiAacSbrNoiseState *state) {
    return mix_internal(tables,energy,env_edges,noise_edges,slots,
                        NULL,0u,input,input_count,output,output_count,state);
}

int sauti_aac_sbr_hf_noise_mix_tonal(
    const SautiAacSbrTables *tables,
    const SautiAacSbrEnergyFrame *energy,
    const uint8_t *env_edges,
    const uint8_t *noise_edges,
    unsigned slots,
    const uint8_t *tone_mask,
    size_t tone_count,
    const SautiAacSbrComplex *input,
    size_t input_count,
    SautiAacSbrComplex *output,
    size_t output_count,
    SautiAacSbrNoiseState *state) {
    if(!tone_mask)return 0;
    return mix_internal(tables,energy,env_edges,noise_edges,slots,
                        tone_mask,tone_count,input,input_count,
                        output,output_count,state);
}
