/* HE-AAC SBR-19: QMF-domain additional sinusoids, with four-phase
 * evolution and previous-frame persistence. The phase/sign convention
 * uses the SBR four-slot oscillator and absolute QMF subband parity.
 * Sinusoid amplitude is sqrt(E/(1+Q)) from decoded SBR envelope/noise.
 * Does not perform complete normative gain/noise/harmonic joint adjustment
 * or QMF analysis/synthesis and does not output PCM.
 */
#include "sauti_aac_sbr_hf_sine.h"
#include <float.h>
#include <math.h>
#include <string.h>

static int geometry_ok(const SautiAacSbrTables *t) {
    if(!t || t->kx<1u || t->kx>32u || t->m<1u || t->m>64u-t->kx ||
       t->n_high<1u || t->n_high>SAUTI_AAC_SBR_MAX_MASTER_BANDS ||
       t->n_low!=(t->n_high+1u)/2u ||
       t->n_noise<1u || t->n_noise>SAUTI_AAC_SBR_MAX_NOISE_BANDS ||
       t->n_noise>t->n_low ||
       t->high[0]!=t->kx || t->high[t->n_high]!=t->kx+t->m ||
       t->low[0]!=t->kx || t->low[t->n_low]!=t->kx+t->m ||
       t->noise[0]!=t->kx || t->noise[t->n_noise]!=t->kx+t->m)
        return 0;
    for(unsigned j=1u;j<=t->n_high;j++)
        if(t->high[j]<=t->high[j-1u])return 0;
    const unsigned odd=t->n_high&1u;
    for(unsigned j=0u;j<=t->n_low;j++){
        const unsigned ix=j?2u*j-odd:0u;
        if(ix>t->n_high || t->low[j]!=t->high[ix])return 0;
    }
    for(unsigned j=1u;j<=t->n_noise;j++){
        if(t->noise[j]<=t->noise[j-1u])return 0;
        int found=0;
        for(unsigned k=1u;k<=t->n_low;k++)
            if(t->noise[j]==t->low[k])found=1;
        if(!found)return 0;
    }
    return 1;
}

int sauti_aac_sbr_hf_sine_mix(
    const SautiAacSbrTables *tables,
    const SautiAacSbrEnergyFrame *energy,
    const uint8_t *harmonic_flags,
    unsigned flag_count,
    unsigned transient_env,
    const uint8_t *env_edges,
    const uint8_t *noise_edges,
    unsigned slots,
    const SautiAacSbrComplex *input,
    size_t input_count,
    SautiAacSbrComplex *output,
    size_t output_count,
    SautiAacSbrSineState *state) {
    if(!geometry_ok(tables) || !energy || !harmonic_flags ||
       !env_edges || !noise_edges || !input || !output || !state ||
       flag_count!=tables->n_high ||
       slots<1u || slots>SAUTI_AAC_SBR_HF_MAX_SLOTS ||
       input_count<(size_t)slots*SAUTI_AAC_SBR_HF_HIGH_BANDS ||
       output_count<(size_t)slots*SAUTI_AAC_SBR_HF_HIGH_BANDS ||
       energy->env_count<1u || energy->env_count>SAUTI_AAC_SBR_MAX_ENV ||
       energy->noise_count!=(energy->env_count==1u?1u:2u) ||
       transient_env>energy->env_count ||
       env_edges[0]!=0u || noise_edges[0]!=0u ||
       env_edges[energy->env_count]!=slots ||
       noise_edges[energy->noise_count]!=slots ||
       state->ready>1u || state->phase>3u)
        return 0;
    for(unsigned k=0u;k<SAUTI_AAC_SBR_HF_HIGH_BANDS;k++)
        if(state->previous[k]>1u ||
           (!state->ready && state->previous[k]!=0u))return 0;
    if(!state->ready && state->phase!=0u)return 0;
    for(unsigned i=0u;i<flag_count;i++)
        if(harmonic_flags[i]>1u)return 0;
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
    for(unsigned n=1u;n<energy->noise_count;n++){
        int found=0;
        for(unsigned e=1u;e<energy->env_count;e++)
            if(noise_edges[n]==env_edges[e])found=1;
        if(!found)return 0;
    }

    unsigned midpoint[SAUTI_AAC_SBR_MAX_MASTER_BANDS];
    uint8_t marked[SAUTI_AAC_SBR_HF_HIGH_BANDS]={0};
    for(unsigned i=0u;i<flag_count;i++){
        const unsigned mid=(tables->high[i]+tables->high[i+1u])/2u;
        if(mid<tables->kx || mid>=tables->kx+tables->m ||
           marked[mid])return 0;
        marked[mid]=harmonic_flags[i];
        midpoint[i]=mid;
    }
    /* Save target power per high QMF bin for each envelope. */
    double target[SAUTI_AAC_SBR_MAX_ENV][64]={0};
    for(unsigned e=0u;e<energy->env_count;e++){
        const uint8_t *freq=energy->freq_res[e]?
                           tables->high:tables->low;
        const unsigned n=energy->freq_res[e]?
                         tables->n_high:tables->n_low;
        for(unsigned b=0u;b<n;b++)
            for(unsigned k=freq[b];k<freq[b+1u];k++)
                target[e][k]=energy->env_energy[e][b];
    }
    const size_t count=(size_t)slots*SAUTI_AAC_SBR_HF_HIGH_BANDS;
    SautiAacSbrComplex next[
        SAUTI_AAC_SBR_HF_MAX_SLOTS*SAUTI_AAC_SBR_HF_HIGH_BANDS];
    memcpy(next,input,count*sizeof(next[0]));
    SautiAacSbrSineState updated=*state;
    updated.ready=1u;
    unsigned e=0u,q=0u;
    for(unsigned t=0u;t<slots;t++){
        while(e+1u<energy->env_count && t>=env_edges[e+1u])e++;
        while(q+1u<energy->noise_count && t>=noise_edges[q+1u])q++;
        unsigned band=0u;
        for(unsigned k=tables->kx;k<tables->kx+tables->m;k++){
            while(band+1u<tables->n_noise &&
                  k>=tables->noise[band+1u])band++;
            const size_t idx=(size_t)t*SAUTI_AAC_SBR_HF_HIGH_BANDS+k;
            if(!isfinite(input[idx].re) || !isfinite(input[idx].im))
                return 0;
        }
        for(unsigned i=0u;i<flag_count;i++){
            if(!harmonic_flags[i])continue;
            const unsigned k=midpoint[i];
            const int continued=state->ready && state->previous[k];
            if(e<transient_env && !continued)continue;
            unsigned g=0u;
            while(g+1u<tables->n_noise &&
                  k>=tables->noise[g+1u])g++;
            const double power=target[e][k];
            const double qval=energy->noise_factor[q][g];
            const double amp=sqrt(power/(1.0+qval));
            if(!isfinite(amp) || amp>FLT_MAX)return 0;
            /* indexsine 0: real +, 1: imaginary alternating signs,
             * 2: real -, 3: imaginary alternating with opposite signs.
             * Odd kx reverses the imaginary sign relative to even kx. */
            const unsigned phase=updated.phase;
            const unsigned parity=k&1u;
            double sign=1.0;
            if(phase==2u || (phase==1u && parity) ||
               (phase==3u && !parity))
                sign=-1.0;
            const size_t idx=(size_t)t*SAUTI_AAC_SBR_HF_HIGH_BANDS+k;
            const double re=(double)input[idx].re+
                            ((phase&1u)?0.0:sign*amp);
            const double im=(double)input[idx].im+
                            ((phase&1u)?sign*amp:0.0);
            if(!isfinite(re) || !isfinite(im) ||
               fabs(re)>FLT_MAX || fabs(im)>FLT_MAX)
                return 0;
            next[idx].re=(float)re;
            next[idx].im=(float)im;
        }
        updated.phase=(uint8_t)((updated.phase+1u)&3u);
    }
    memset(updated.previous,0,sizeof(updated.previous));
    for(unsigned i=0u;i<flag_count;i++){
        const unsigned mid=midpoint[i];
        updated.previous[mid]=(uint8_t)(
            harmonic_flags[i] &&
            ((energy->env_count-1u)>=transient_env ||
             (state->ready && state->previous[mid])));
    }
    memcpy(output,next,count*sizeof(next[0]));
    *state=updated;
    return 1;
}
