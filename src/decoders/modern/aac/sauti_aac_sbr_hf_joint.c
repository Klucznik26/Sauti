/* HE-AAC SBR-20: joint QMF HF adjustment.
 * Stage independent module results locally. A frame only commits after
 * limiter, noise and sine all succeed; no partial channel-history update.
 * Tonal bins mask random noise and reserve their energy for the sine.
 * No external codecs or heap allocation; no QMF synthesis or PCM.
 */
#include "sauti_aac_sbr_hf_joint.h"
#include <string.h>

int sauti_aac_sbr_hf_joint_process(
    const SautiAacSbrHfJointConfig *config,
    const SautiAacSbrComplex *input,
    size_t input_count,
    SautiAacSbrComplex *output,
    size_t output_count,
    SautiAacSbrHfJointState *state,
    SautiAacSbrLimiterReport *report) {
    if(!config || !input || !output || !state || !report ||
       !config->tables || !config->patches || !config->energy ||
       !config->env_edges || !config->noise_edges ||
       !config->harmonic_flags ||
       config->slots<1u || config->slots>SAUTI_AAC_SBR_HF_MAX_SLOTS ||
       input_count<(size_t)config->slots*SAUTI_AAC_SBR_HF_HIGH_BANDS ||
       output_count<(size_t)config->slots*SAUTI_AAC_SBR_HF_HIGH_BANDS ||
       config->flag_count!=config->tables->n_high ||
       config->energy->env_count<1u ||
       config->energy->env_count>SAUTI_AAC_SBR_MAX_ENV ||
       config->transient_env>config->energy->env_count ||
       config->limiter_mode>3u || config->gain_mode>3u)
        return 0;

    /* Validate entire SBR-17 frequency-map geometry before indexing
     * high-band boundaries, including f_tablehigh[] and f_tablelow[]. */
    SautiAacSbrLimiterBands limiter_bands;
    if(!sauti_aac_sbr_limiter_bands_build(
        config->tables,config->patches,config->limiter_mode,
        &limiter_bands))return 0;

    const unsigned nenv=config->energy->env_count;
    const unsigned nband=config->flag_count;
    if(config->env_edges[0]!=0u ||
       config->env_edges[nenv]!=config->slots)return 0;
    for(unsigned e=0u;e<nenv;e++)
        if(config->env_edges[e]>=config->env_edges[e+1u] ||
           config->env_edges[e+1u]>config->slots)return 0;

    uint8_t occupied[SAUTI_AAC_SBR_HF_MAX_SLOTS*
                     SAUTI_AAC_SBR_HF_HIGH_BANDS];
    memset(occupied,0,sizeof(occupied));
    for(unsigned b=0u;b<nband;b++){
        const uint8_t flag=config->harmonic_flags[b];
        if(flag>1u)return 0;
        if(!flag)continue;
        const unsigned k=(config->tables->high[b]+
                          config->tables->high[b+1u])/2u;
        if(k<config->tables->kx ||
           k>=config->tables->kx+config->tables->m ||
           k>=SAUTI_AAC_SBR_HF_HIGH_BANDS)
            return 0;

        const int continued=state->sine.ready &&
                            state->sine.previous[k]==1u;
        for(unsigned e=0;e<nenv;e++){
            if(e<config->transient_env && !continued)continue;
            for(unsigned t=config->env_edges[e];
                         t<config->env_edges[e+1u];t++)
                occupied[(size_t)t*SAUTI_AAC_SBR_HF_HIGH_BANDS+k]=1u;
        }
    }

    const size_t used=(size_t)config->slots*
                      SAUTI_AAC_SBR_HF_HIGH_BANDS;
    SautiAacSbrComplex limited[
        SAUTI_AAC_SBR_HF_MAX_SLOTS*SAUTI_AAC_SBR_HF_HIGH_BANDS];
    SautiAacSbrComplex mixed[
        SAUTI_AAC_SBR_HF_MAX_SLOTS*SAUTI_AAC_SBR_HF_HIGH_BANDS];
    SautiAacSbrComplex finished[
        SAUTI_AAC_SBR_HF_MAX_SLOTS*SAUTI_AAC_SBR_HF_HIGH_BANDS];
    SautiAacSbrLimiterReport stats;
    SautiAacSbrHfJointState next=*state;

    if(!sauti_aac_sbr_hf_limit(
        config->tables,config->energy,config->env_edges,config->slots,
        limiter_bands.edges,limiter_bands.count,config->gain_mode,
        input,input_count,limited,used,&stats))
        return 0;

    if(!sauti_aac_sbr_hf_noise_mix_tonal(
        config->tables,config->energy,config->env_edges,
        config->noise_edges,config->slots,occupied,used,
        limited,used,mixed,used,&next.noise))
        return 0;

    if(!sauti_aac_sbr_hf_sine_mix(
        config->tables,config->energy,config->harmonic_flags,
        config->flag_count,config->transient_env,
        config->env_edges,config->noise_edges,config->slots,
        mixed,used,finished,used,&next.sine))
        return 0;

    memcpy(output,finished,used*sizeof(finished[0]));
    *report=stats;
    *state=next;
    return 1;
}
