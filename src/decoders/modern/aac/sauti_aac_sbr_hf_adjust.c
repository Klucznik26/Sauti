/* SBR-15: amplitude-only HF envelope matching in COMPLEX QMF domain.
 * This is a bounded, fail-closed precursor to full normative adjustment:
 * noise/sine injection, limiter, boost, smoothing are NOT implemented.
 * No allocation, no external codec, and no PCM output. */
#include "sauti_aac_sbr_hf_adjust.h"
#include <float.h>
#include <math.h>
#include <string.h>

static int valid_tables(const SautiAacSbrTables *t) {
    if(!t || t->kx<1u || t->kx>32u ||
       t->m<1u || t->m>64u-t->kx ||
       t->n_high<1u || t->n_high>SAUTI_AAC_SBR_MAX_MASTER_BANDS ||
       t->n_low!=(t->n_high+1u)/2u ||
       t->n_noise<1u || t->n_noise>SAUTI_AAC_SBR_MAX_NOISE_BANDS ||
       t->n_noise>t->n_low ||
       t->high[0]!=t->kx ||
       t->high[t->n_high]!=t->kx+t->m ||
       t->low[0]!=t->kx ||
       t->low[t->n_low]!=t->kx+t->m ||
       t->noise[0]!=t->kx ||
       t->noise[t->n_noise]!=t->kx+t->m)
        return 0;
    for(unsigned j=1;j<=t->n_high;j++)
        if(t->high[j]<=t->high[j-1u])return 0;
    const unsigned odd=t->n_high&1u;
    for(unsigned j=0;j<=t->n_low;j++){
        const unsigned ix=j?2u*j-odd:0u;
        if(ix>t->n_high || t->low[j]!=t->high[ix])return 0;
    }
    for(unsigned j=1;j<=t->n_noise;j++){
        if(t->noise[j]<=t->noise[j-1u])return 0;
        int found=0;
        for(unsigned k=1;k<=t->n_low;k++)
            if(t->noise[j]==t->low[k])found=1;
        if(!found)return 0;
    }
    return 1;
}

int sauti_aac_sbr_hf_adjust(const SautiAacSbrTables *tables,
                            const SautiAacSbrEnergyFrame *energy,
                            const uint8_t *slot_borders,
                            unsigned slots,
                            unsigned interpolation,
                            const SautiAacSbrComplex *input,
                            size_t input_count,
                            SautiAacSbrComplex *output,
                            size_t output_count) {
    if(!energy || !slot_borders || !input || !output ||
       !valid_tables(tables) || slots<1u ||
       slots>SAUTI_AAC_SBR_HF_MAX_SLOTS || interpolation>1u ||
       input_count<(size_t)slots*SAUTI_AAC_SBR_HF_HIGH_BANDS ||
       output_count<(size_t)slots*SAUTI_AAC_SBR_HF_HIGH_BANDS ||
       energy->env_count<1u ||
       energy->env_count>SAUTI_AAC_SBR_MAX_ENV ||
       energy->noise_count!=(energy->env_count==1u?1u:2u) ||
       slot_borders[0]!=0u ||
       slot_borders[energy->env_count]!=slots)
        return 0;

    for(unsigned e=0;e<energy->env_count;e++) {
        if(slot_borders[e]>=slot_borders[e+1u] ||
           slot_borders[e+1u]>slots ||
           energy->freq_res[e]>1u)
            return 0;
        const unsigned count=energy->freq_res[e]?
                             tables->n_high:tables->n_low;
        for(unsigned b=0;b<count;b++)
            if(!isfinite(energy->env_energy[e][b]) ||
               energy->env_energy[e][b]<0.0)
                return 0;
    }

    const size_t samples=(size_t)slots*SAUTI_AAC_SBR_HF_HIGH_BANDS;
    SautiAacSbrComplex next[
        SAUTI_AAC_SBR_HF_MAX_SLOTS*SAUTI_AAC_SBR_HF_HIGH_BANDS];
    memcpy(next,input,samples*sizeof(next[0]));

    for(unsigned e=0;e<energy->env_count;e++) {
        const uint8_t *bounds=energy->freq_res[e]?
                              tables->high:tables->low;
        const unsigned count=energy->freq_res[e]?
                             tables->n_high:tables->n_low;
        const unsigned from=slot_borders[e];
        const unsigned to=slot_borders[e+1u];
        for(unsigned b=0;b<count;b++){
            const double target=energy->env_energy[e][b];
            const unsigned begin=bounds[b],end=bounds[b+1u];
            if(begin<tables->kx || end>tables->kx+tables->m ||
               begin>=end)return 0;
            for(unsigned group=begin;group<end;){
                const unsigned limit=interpolation?group+1u:end;
                double sum=0.0;
                for(unsigned time=from;time<to;time++)
                    for(unsigned k=group;k<limit;k++) {
                        const SautiAacSbrComplex x=
                            input[(size_t)time*SAUTI_AAC_SBR_HF_HIGH_BANDS+k];
                        if(!isfinite(x.re) || !isfinite(x.im))return 0;
                        const double re=x.re,im=x.im;
                        sum+=re*re+im*im;
                        if(!isfinite(sum))return 0;
                    }
                const double mean=sum/((double)(to-from)*(limit-group));
                if(target>0.0 && mean==0.0)return 0;
                const double gain=target==0.0?0.0:sqrt(target/mean);
                if(!isfinite(gain))return 0;
                for(unsigned time=from;time<to;time++)
                    for(unsigned k=group;k<limit;k++) {
                        const size_t idx=
                            (size_t)time*SAUTI_AAC_SBR_HF_HIGH_BANDS+k;
                        const double re=(double)input[idx].re*gain;
                        const double im=(double)input[idx].im*gain;
                        if(!isfinite(re)||!isfinite(im) ||
                           fabs(re)>FLT_MAX || fabs(im)>FLT_MAX)
                            return 0;
                        next[idx].re=(float)re;
                        next[idx].im=(float)im;
                    }
                group=limit;
            }
        }
    }
    memcpy(output,next,samples*sizeof(next[0]));
    return 1;
}
