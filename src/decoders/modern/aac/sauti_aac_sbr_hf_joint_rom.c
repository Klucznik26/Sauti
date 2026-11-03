/* SBR-24: 512-vector MPEG-4 SBR noise ROM integrated into the
 * coefficient-smoothed HF limiter/noise/harmonic chain. This is a
 * separate entry point preserving prior SBR-20/22 behavior.
 * A frame commits noise index, sine phase, smoothing history, output
 * and limiter diagnostics only after ALL stages succeed. No heap. */
#include "sauti_aac_sbr_hf_joint_rom.h"
#include <math.h>
#include <string.h>

int sauti_aac_sbr_hf_joint_process_rom(
    const SautiAacSbrHfJointConfig *config,
    const uint8_t *attack,
    unsigned bs_smoothing_mode,
    const SautiAacSbrComplex *input,
    size_t input_count,
    SautiAacSbrComplex *output,
    size_t output_count,
    SautiAacSbrHfRomState *state,
    SautiAacSbrLimiterReport *report) {
    if(!config || !attack || !input || !output || !state || !report ||
       !config->tables || !config->patches || !config->energy ||
       !config->env_edges || !config->noise_edges ||
       !config->harmonic_flags ||
       config->slots<1u || config->slots>SAUTI_AAC_SBR_HF_MAX_SLOTS ||
       input_count<(size_t)config->slots*SAUTI_AAC_SBR_HF_HIGH_BANDS ||
       output_count<(size_t)config->slots*SAUTI_AAC_SBR_HF_HIGH_BANDS ||
       config->energy->env_count<1u ||
       config->energy->env_count>SAUTI_AAC_SBR_MAX_ENV ||
       config->energy->noise_count!=
            (config->energy->env_count==1u?1u:2u) ||
       config->flag_count!=config->tables->n_high ||
       config->transient_env>config->energy->env_count ||
       bs_smoothing_mode>1u ||
       config->gain_mode>3u || config->limiter_mode>3u)
        return 0;

    const unsigned env_count=config->energy->env_count;
    const unsigned noise_count=config->energy->noise_count;
    const unsigned slots=config->slots;
    const SautiAacSbrTables *tab=config->tables;
    if(config->env_edges[0]!=0u ||
       config->env_edges[env_count]!=slots ||
       config->noise_edges[0]!=0u ||
       config->noise_edges[noise_count]!=slots)return 0;
    for(unsigned e=0;e<env_count;e++)
        if(config->env_edges[e]>=config->env_edges[e+1u] ||
           config->env_edges[e+1u]>slots ||
           attack[e]>1u)return 0;
    for(unsigned n=0;n<noise_count;n++)
        if(config->noise_edges[n]>=config->noise_edges[n+1u] ||
           config->noise_edges[n+1u]>slots)return 0;
    for(unsigned n=1;n<noise_count;n++){
        int found=0;
        for(unsigned e=1;e<env_count;e++)
            if(config->noise_edges[n]==config->env_edges[e])
                found=1;
        if(!found)return 0;
    }

    /* Validate patch/table geometry before using high[] or noise[]. */
    SautiAacSbrLimiterBands limiter_bands;
    if(!sauti_aac_sbr_limiter_bands_build(tab,config->patches,
         config->limiter_mode,&limiter_bands))return 0;

    const size_t count=(size_t)slots*SAUTI_AAC_SBR_HF_HIGH_BANDS;
    SautiAacSbrComplex limited[
        SAUTI_AAC_SBR_HF_MAX_SLOTS*SAUTI_AAC_SBR_HF_HIGH_BANDS];
    SautiAacSbrComplex mixed[
        SAUTI_AAC_SBR_HF_MAX_SLOTS*SAUTI_AAC_SBR_HF_HIGH_BANDS];
    SautiAacSbrComplex finished[
        SAUTI_AAC_SBR_HF_MAX_SLOTS*SAUTI_AAC_SBR_HF_HIGH_BANDS];
    SautiAacSbrLimiterReport stats;
    SautiAacSbrHfRomState next=*state;

    if(!sauti_aac_sbr_hf_limit(
        tab,config->energy,config->env_edges,slots,
        limiter_bands.edges,limiter_bands.count,config->gain_mode,
        input,input_count,limited,count,&stats))return 0;

    /* Precompute amplitude targets for EVERY envelope, including tone
     * masks which may switch on at transient_env. The SBR-21 filter
     * smooths these coefficients only, never complex QMF samples. */
    double gains[SAUTI_AAC_SBR_SMOOTH_MAX_ENV]
                [SAUTI_AAC_SBR_SMOOTH_BANDS]={0};
    double noises[SAUTI_AAC_SBR_SMOOTH_MAX_ENV]
                 [SAUTI_AAC_SBR_SMOOTH_BANDS]={0};
    for(unsigned e=0;e<env_count;e++){
        const unsigned time=config->env_edges[e];
        unsigned ng=0;
        while(ng+1u<noise_count &&
              time>=config->noise_edges[ng+1u])ng++;
        if(time<config->noise_edges[ng] ||
           time>=config->noise_edges[ng+1u])return 0;

        const unsigned resolution=config->energy->freq_res[e];
        if(resolution>1u)return 0;
        const uint8_t *freq=resolution?tab->high:tab->low;
        const unsigned bands=resolution?tab->n_high:tab->n_low;
        double target[SAUTI_AAC_SBR_HF_HIGH_BANDS]={0};
        for(unsigned b=0;b<bands;b++)
            for(unsigned k=freq[b];k<freq[b+1u];k++)
                target[k]=config->energy->env_energy[e][b];

        unsigned noise_band=0;
        for(unsigned k=tab->kx;k<tab->kx+tab->m;k++){
            while(noise_band+1u<tab->n_noise &&
                  k>=tab->noise[noise_band+1u])noise_band++;
            if(k<tab->noise[noise_band] ||
               k>=tab->noise[noise_band+1u])return 0;
            const double q=config->energy->noise_factor[ng][noise_band];
            const double power=target[k];
            if(!isfinite(q) || q<0.0 ||
               !isfinite(power) || power<0.0)return 0;

            int tonal=0;
            for(unsigned b=0;b<config->flag_count;b++){
                const unsigned mid=(tab->high[b]+tab->high[b+1u])/2u;
                if(config->harmonic_flags[b]>1u)return 0;
                if(config->harmonic_flags[b] && mid==k &&
                   (e>=config->transient_env ||
                    (next.sine.ready &&
                     next.sine.previous[k]==1u)))
                    tonal=1;
            }
            /* Avoid q/(1+q) overflow for extremely large finite Q. */
            const double signal_share=1.0/(1.0+q);
            const double noise_share=1.0-signal_share;
            const double signal_ratio=tonal?sqrt(noise_share):
                                            sqrt(signal_share);
            const double gain=stats.applied_gain[e][k]*signal_ratio;
            const double amp=tonal?0.0:sqrt(power)*sqrt(noise_share);
            if(!isfinite(gain) || !isfinite(amp) ||
               gain<0.0 || amp<0.0)return 0;
            gains[e][k]=gain;
            noises[e][k]=amp;
        }
    }

    SautiAacSbrSmoothFrame smooth;
    if(!sauti_aac_sbr_smooth_coefficients(
        tab,env_count,config->env_edges,slots,
        gains,noises,attack,bs_smoothing_mode,&next.smooth,
        &smooth))return 0;

    if(!sauti_aac_sbr_noise_rom_apply_levels(
        tab,slots,smooth.gain,smooth.noise,
        input,input_count,mixed,count,&next.noise_rom))return 0;

    if(!sauti_aac_sbr_hf_sine_mix(
        tab,config->energy,config->harmonic_flags,
        config->flag_count,config->transient_env,
        config->env_edges,config->noise_edges,slots,
        mixed,count,finished,count,&next.sine))return 0;

    memcpy(output,finished,count*sizeof(finished[0]));
    *report=stats;
    *state=next;
    return 1;
}
