/* HE-AAC SBR-53g: staging-only complex HF + raw ROM noise mix.
 * Not normative final SBR sound; no PCM, smoothing, or sine synthesis. */
#include "sauti_aac_sbr_temporal_mix.h"
#include <float.h>
#include <math.h>
#include <string.h>

#define MIX_SLOTS 32u
#define MIX_BANDS 64u
#define MIX_COUNT (MIX_SLOTS*MIX_BANDS)

int sauti_aac_sbr_temporal_noise_mix_preview(
    const SautiAacSbrComplex *adjusted_hf,size_t input_count,
    const SautiAacSbrTemporalNoiseFrame *noise,
    const SautiAacSbrTemporalApplyReport *apply_report,
    const SautiAacSbrTemporalNoiseReport *noise_report,
    SautiAacSbrComplex *output,size_t output_count,
    SautiAacSbrTemporalMixReport *report){
    if(!adjusted_hf || !noise || !apply_report || !noise_report ||
       !output || !report ||
       input_count<MIX_COUNT || output_count<MIX_COUNT ||
       apply_report->emitted!=1u || noise_report->emitted!=1u ||
       apply_report->frame_index!=noise_report->frame_index ||
       noise_report->vectors> MIX_COUNT ||
       noise_report->nonzero_amplitudes>noise_report->vectors ||
       noise_report->rom_index>=SAUTI_AAC_SBR_NOISE_ROM_SIZE)
        return 0;
    /* If N active vectors end at index I, the first vector's
     * ROM index is (I-N+1) modulo 512. This independently checks
     * the supplied ROM chronology and mask, including silent bins. */
    const unsigned n=noise_report->vectors;
    unsigned index=(noise_report->rom_index +
                    SAUTI_AAC_SBR_NOISE_ROM_SIZE -
                    (n % SAUTI_AAC_SBR_NOISE_ROM_SIZE)) & 511u;
    SautiAacSbrComplex next[MIX_COUNT];
    unsigned count=0u,positive=0u;
    double square=0.0;
    for(unsigned i=0u;i<MIX_COUNT;i++){
        const SautiAacSbrComplex x=adjusted_hf[i];
        const SautiAacSbrComplex v=noise->vector[i];
        const double a=noise->raw_amplitude[i];
        const unsigned active=noise->active[i];
        if(active>1u ||
           !isfinite(x.re) || !isfinite(x.im) ||
           !isfinite(v.re) || !isfinite(v.im) ||
           !isfinite(a) || a<0.0)
            return 0;
        if(!active){
            if(a!=0.0 || v.re!=0.0f || v.im!=0.0f)
                return 0;
            next[i]=x;
            continue;
        }
        index=(index+1u)&511u;
        SautiAacSbrComplex expected;
        if(!sauti_aac_sbr_noise_rom_lookup(index,&expected) ||
           memcmp(&expected,&v,sizeof(v))!=0)return 0;
        ++count;
        if(a>0.0)++positive;
        const double re=(double)v.re*a;
        const double im=(double)v.im*a;
        square+=re*re+im*im;
        if(!isfinite(re) || !isfinite(im) || !isfinite(square))
            return 0;
        const double mixed_re=(double)x.re+re;
        const double mixed_im=(double)x.im+im;
        if(!isfinite(mixed_re) || !isfinite(mixed_im) ||
           fabs(mixed_re)>FLT_MAX || fabs(mixed_im)>FLT_MAX)
            return 0;
        next[i].re=(float)mixed_re;
        next[i].im=(float)mixed_im;
    }
    if(count!=n || positive!=noise_report->nonzero_amplitudes ||
       index!=noise_report->rom_index)return 0;

    SautiAacSbrTemporalMixReport stats={0};
    stats.frame_index=apply_report->frame_index;
    stats.active_bins=count;
    stats.noise_bins=positive;
    stats.noise_rms=count?sqrt(square/(double)count):0.0;
    if(!isfinite(stats.noise_rms))return 0;
    memcpy(output,next,sizeof(next));
    *report=stats;
    return 1;
}
