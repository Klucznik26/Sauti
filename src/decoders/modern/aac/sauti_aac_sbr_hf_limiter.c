/* HE-AAC SBR-16: QMF-domain gain cap and capped restoration boost.
 * This step assumes a caller-provided limiter frequency partition.
 * It does not yet include noise/sinusoid injection, transient-aware
 * adjustment, smoothing, or production QMF filter-bank normalization.
 * On ANY failure neither output samples nor report are modified. */
#include "sauti_aac_sbr_hf_limiter.h"
#include <float.h>
#include <math.h>
#include <string.h>

static int tables_valid(const SautiAacSbrTables *t) {
    if(!t || t->kx<1u || t->kx>32u || t->m<1u || t->m>64u-t->kx ||
       t->n_high<1u || t->n_high>SAUTI_AAC_SBR_MAX_MASTER_BANDS ||
       t->n_low!=(t->n_high+1u)/2u ||
       t->n_noise<1u || t->n_noise>SAUTI_AAC_SBR_MAX_NOISE_BANDS ||
       t->n_noise>t->n_low ||
       t->high[0]!=t->kx || t->high[t->n_high]!=t->kx+t->m ||
       t->low[0]!=t->kx || t->low[t->n_low]!=t->kx+t->m ||
       t->noise[0]!=t->kx || t->noise[t->n_noise]!=t->kx+t->m)
        return 0;
    for(unsigned i=1;i<=t->n_high;i++)
        if(t->high[i]<=t->high[i-1u])return 0;
    const unsigned odd=t->n_high&1u;
    for(unsigned i=0;i<=t->n_low;i++){
        const unsigned j=i?2u*i-odd:0u;
        if(j>t->n_high || t->low[i]!=t->high[j])return 0;
    }
    for(unsigned i=1;i<=t->n_noise;i++){
        int found=0;
        if(t->noise[i]<=t->noise[i-1u])return 0;
        for(unsigned j=1;j<=t->n_low;j++)
            if(t->low[j]==t->noise[i])found=1;
        if(!found)return 0;
    }
    return 1;
}

int sauti_aac_sbr_hf_limit(const SautiAacSbrTables *tables,
                            const SautiAacSbrEnergyFrame *energy,
                            const uint8_t *slot_borders,
                            unsigned slots,
                            const uint8_t *limiter_edges,
                            unsigned limiter_count,
                            unsigned gain_mode,
                            const SautiAacSbrComplex *input,
                            size_t input_count,
                            SautiAacSbrComplex *output,
                            size_t output_count,
                            SautiAacSbrLimiterReport *report) {
    if(!tables_valid(tables) || !energy || !slot_borders ||
       !limiter_edges || !input || !output || !report ||
       slots<1u || slots>SAUTI_AAC_SBR_HF_MAX_SLOTS ||
       limiter_count<1u || limiter_count>SAUTI_AAC_SBR_MAX_LIMITER_BANDS ||
       limiter_count>tables->m || gain_mode>3u ||
       input_count<(size_t)slots*SAUTI_AAC_SBR_HF_HIGH_BANDS ||
       output_count<(size_t)slots*SAUTI_AAC_SBR_HF_HIGH_BANDS ||
       energy->env_count<1u || energy->env_count>SAUTI_AAC_SBR_MAX_ENV ||
       energy->noise_count!=(energy->env_count==1u?1u:2u) ||
       slot_borders[0]!=0u || slot_borders[energy->env_count]!=slots ||
       limiter_edges[0]!=tables->kx ||
       limiter_edges[limiter_count]!=tables->kx+tables->m)
        return 0;

    for(unsigned i=1;i<=limiter_count;i++)
        if(limiter_edges[i]<=limiter_edges[i-1u] ||
           limiter_edges[i]>tables->kx+tables->m)return 0;
    for(unsigned e=0;e<energy->env_count;e++){
        if(slot_borders[e]>=slot_borders[e+1u] ||
           slot_borders[e+1u]>slots ||
           energy->freq_res[e]>1u)return 0;
        const unsigned n=energy->freq_res[e]?
                         tables->n_high:tables->n_low;
        for(unsigned j=0;j<n;j++)
            if(!isfinite(energy->env_energy[e][j]) ||
               energy->env_energy[e][j]<0.0)return 0;
    }

    const size_t used=(size_t)slots*SAUTI_AAC_SBR_HF_HIGH_BANDS;
    SautiAacSbrComplex next[
        SAUTI_AAC_SBR_HF_MAX_SLOTS*SAUTI_AAC_SBR_HF_HIGH_BANDS];
    memcpy(next,input,used*sizeof(next[0]));

    SautiAacSbrLimiterReport stats;
    memset(&stats,0,sizeof(stats));
    stats.env_count=energy->env_count;
    stats.limiter_count=limiter_count;

    static const double limit_gain[4]={
        0.70795,1.0,1.41254,10000000000.0
    };
    const double max_boost=1.584893192;
    const unsigned kx=tables->kx;
    const unsigned k2=tables->kx+tables->m;

    for(unsigned e=0;e<energy->env_count;e++){
        double target[SAUTI_AAC_SBR_HF_HIGH_BANDS]={0};
        double current[SAUTI_AAC_SBR_HF_HIGH_BANDS]={0};
        double gains[SAUTI_AAC_SBR_HF_HIGH_BANDS]={0};
        const unsigned a=slot_borders[e],z=slot_borders[e+1u];
        const uint8_t *freq=energy->freq_res[e]?
                            tables->high:tables->low;
        const unsigned n=energy->freq_res[e]?
                         tables->n_high:tables->n_low;

        for(unsigned b=0;b<n;b++)
            for(unsigned k=freq[b];k<freq[b+1u];k++)
                target[k]=energy->env_energy[e][b];

        /* One average source energy per QMF subband, over the
         * time support of this envelope. */
        for(unsigned k=kx;k<k2;k++){
            double sum=0.0;
            for(unsigned t=a;t<z;t++){
                const SautiAacSbrComplex x=
                    input[(size_t)t*SAUTI_AAC_SBR_HF_HIGH_BANDS+k];
                if(!isfinite(x.re)||!isfinite(x.im))return 0;
                const double re=x.re,im=x.im;
                sum+=re*re+im*im;
                if(!isfinite(sum))return 0;
            }
            current[k]=sum/(z-a);
            if(target[k]>0.0 && current[k]==0.0)return 0;
        }

        for(unsigned b=0;b<limiter_count;b++){
            const unsigned left=limiter_edges[b];
            const unsigned right=limiter_edges[b+1u];
            double sum_target=0.0,sum_current=0.0;
            for(unsigned k=left;k<right;k++){
                sum_target+=target[k];
                sum_current+=current[k];
            }
            if(!isfinite(sum_target)||!isfinite(sum_current))
                return 0;
            double max_gain=0.0;
            if(sum_target>0.0 && sum_current>0.0){
                const double ideal=sqrt(sum_target/sum_current);
                max_gain=fmin(100000.0,limit_gain[gain_mode]*ideal);
            }
            if(!isfinite(max_gain))return 0;
            stats.limiter_max[e][b]=max_gain;

            double after=0.0;
            for(unsigned k=left;k<right;k++){
                const double ideal=target[k]==0.0?
                    0.0:sqrt(target[k]/current[k]);
                gains[k]=fmin(ideal,max_gain);
                after+=current[k]*gains[k]*gains[k];
            }
            if(!isfinite(after))return 0;
            if(sum_target>0.0 && after==0.0)return 0;
            const double boost=sum_target==0.0?1.0:
                                fmin(max_boost,sqrt(sum_target/after));
            if(!isfinite(boost))return 0;
            stats.limiter_boost[e][b]=boost;
            for(unsigned k=left;k<right;k++){
                gains[k]*=boost;
                if(!isfinite(gains[k]))return 0;
                stats.applied_gain[e][k]=gains[k];
            }
        }

        /* Validate all scaled values before publishing anything. */
        for(unsigned t=a;t<z;t++)
            for(unsigned k=kx;k<k2;k++){
                const size_t ix=(size_t)t*SAUTI_AAC_SBR_HF_HIGH_BANDS+k;
                const double re=(double)input[ix].re*gains[k];
                const double im=(double)input[ix].im*gains[k];
                if(!isfinite(re)||!isfinite(im) ||
                   fabs(re)>FLT_MAX || fabs(im)>FLT_MAX)
                    return 0;
                next[ix].re=(float)re;
                next[ix].im=(float)im;
            }
    }
    /* Both outputs are staged. No error is possible after this point. */
    memcpy(output,next,used*sizeof(next[0]));
    *report=stats;
    return 1;
}
