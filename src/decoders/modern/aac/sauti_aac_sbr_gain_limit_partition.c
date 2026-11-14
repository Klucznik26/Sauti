/* SBR-54k: full limiter-band HF/noise/sinusoid gain partition.
 * Numerics follow FFmpeg libavcodec/aacsbr.c:sbr_gain_calc().
 * Diagnostic-only: not connected to the legacy SBR-53d/54a PCM. */
#include "sauti_aac_sbr_gain_limit_partition.h"
#include <float.h>
#include <math.h>
#include <string.h>

#define QMF_BANDS 64u

int sauti_aac_sbr_gain_limit_partition_flags(
    const double e[64],const double ec[64],const double q[64],
    const uint8_t sine_index[64],const uint8_t sine_mapped[64],
    const uint8_t edges[65],
    unsigned group_count,unsigned gain_mode,unsigned delta,
    SautiAacSbrGainLimitedFrame *out){
    if(!e || !ec || !q || !sine_index || !sine_mapped || !edges || !out ||
       group_count<1u || group_count>QMF_BANDS ||
       gain_mode>3u || delta>1u ||
       edges[0]<1u || edges[group_count]>QMF_BANDS)
        return 0;
    for(unsigned j=0u;j<group_count;j++)
        if(edges[j]>=edges[j+1u])return 0;

    static const double max_gain[4]={
        0.70795,1.0,1.41254,10000000000.0
    };
    const double max_boost=1.584893192;
    SautiAacSbrGainLimitedFrame staged={0};
    staged.groups=group_count;
    for(unsigned g=0u;g<group_count;g++){
        const unsigned a=edges[g],z=edges[g+1u];
        double sum_e=0.0,sum_ec=0.0;
        for(unsigned k=a;k<z;k++){
            if(!isfinite(e[k]) || !isfinite(ec[k]) ||
               !isfinite(q[k]) || e[k]<0.0 ||
               ec[k]<0.0 || q[k]<0.0 ||
               sine_index[k]>1u || sine_mapped[k]>1u ||
               !sauti_aac_sbr_gain_partition_flags(
                   e[k],ec[k],q[k],
                   sine_index[k],sine_mapped[k],delta,
                   &staged.band[k]))
                return 0;
            sum_e+=e[k];
            sum_ec+=ec[k];
        }
        if(!isfinite(sum_e)||!isfinite(sum_ec))return 0;

        /* FLT_EPSILON matches FFmpeg's nonzero cap denominator.
         * Keep values finite rather than introducing a magic gain
         * when all band energies are zero. */
        const double ratio=(FLT_EPSILON+sum_e)/
                           (FLT_EPSILON+sum_ec);
        if(!isfinite(ratio) || ratio<0.0)return 0;
        const double cap=fmin(100000.0,max_gain[gain_mode]*sqrt(ratio));
        if(!isfinite(cap) || cap<0.0)return 0;
        staged.group_cap[g]=cap;

        for(unsigned k=a;k<z;k++){
            SautiAacSbrGainPartition *p=&staged.band[k];
            const double old_gain=p->hf_gain;
            const double old_noise=p->noise_amplitude;
            /* FFmpeg adds FLT_MIN to initial gain. This division
             * preserves the same behavior for zero-gain bins. */
            const double q_cap=old_noise*cap/
                               fmax(old_gain,FLT_MIN);
            if(isnan(q_cap) || q_cap<0.0)return 0;
            if(q_cap<old_noise){
                p->noise_amplitude=q_cap;
                staged.noise_capped_bins++;
            }
            if(cap<old_gain){
                p->hf_gain=cap;
                staged.gain_capped_bins++;
            }
            if(sine_index[k])staged.sine_bins++;
        }

        double after=0.0;
        for(unsigned k=a;k<z;k++){
            const SautiAacSbrGainPartition *p=&staged.band[k];
            const double gain_sq=p->hf_gain*p->hf_gain;
            const double sine_sq=p->sine_amplitude*p->sine_amplitude;
            const double noise_sq=p->noise_amplitude*p->noise_amplitude;
            const double total=ec[k]*gain_sq+sine_sq+
                               ((delta && p->sine_amplitude==0.0)?
                                 noise_sq:0.0);
            if(!isfinite(total) || total<0.0)return 0;
            after+=total;
        }
        if(!isfinite(after))return 0;
        const double boost=fmin(max_boost,
            sqrt((FLT_EPSILON+sum_e)/(FLT_EPSILON+after)));
        if(!isfinite(boost) || boost<0.0)return 0;
        staged.group_boost[g]=boost;
        for(unsigned k=a;k<z;k++){
            SautiAacSbrGainPartition *p=&staged.band[k];
            p->hf_gain*=boost;
            p->noise_amplitude*=boost;
            p->sine_amplitude*=boost;
            if(!isfinite(p->hf_gain) ||
               !isfinite(p->noise_amplitude) ||
               !isfinite(p->sine_amplitude))return 0;
        }
    }
    *out=staged;
    return 1;
}

int sauti_aac_sbr_gain_limit_partition(
    const double e[64],const double ec[64],const double q[64],
    const uint8_t sine[64],const uint8_t edges[65],
    unsigned group_count,unsigned gain_mode,unsigned delta,
    SautiAacSbrGainLimitedFrame *out){
    return sauti_aac_sbr_gain_limit_partition_flags(
        e,ec,q,sine,sine,edges,group_count,gain_mode,delta,out);
}
