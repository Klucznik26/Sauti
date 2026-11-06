/* SBR-60: normalize decoded SBR envelope energy for unit-float QMF. */
#include "sauti_aac_sbr_temporal_energy_units.h"
#include <math.h>

int sauti_aac_sbr_temporal_payload_normalize_qmf_energy(
    const SautiAacSbrPayloadFrame *input,
    SautiAacSbrPayloadFrame *output){
    if(!input || !output ||
       input->energy.env_count<1u ||
       input->energy.env_count>SAUTI_AAC_SBR_MAX_ENV ||
       input->energy.noise_count<1u ||
       input->energy.noise_count>2u ||
       input->energy.env_count!=input->grid.env_count ||
       input->energy.noise_count!=input->grid.noise_count)
        return 0;
    SautiAacSbrPayloadFrame staged=*input;
    for(unsigned e=0u;e<input->energy.env_count;e++){
        if(input->energy.freq_res[e]>1u)return 0;
        const unsigned bands=input->energy.freq_res[e]?
            input->tables.n_high:input->tables.n_low;
        if(bands<1u || bands>SAUTI_AAC_SBR_MAX_MASTER_BANDS)return 0;
        for(unsigned b=0u;b<bands;b++){
            const double value=input->energy.env_energy[e][b];
            const double normalized=ldexp(value,-30);
            if(!isfinite(value) || value<=0.0 ||
               !isfinite(normalized) || normalized<=0.0)return 0;
            staged.energy.env_energy[e][b]=normalized;
        }
    }
    for(unsigned e=0u;e<input->energy.noise_count;e++)
        for(unsigned b=0u;b<input->tables.n_noise;b++){
            const double value=input->energy.noise_factor[e][b];
            if(!isfinite(value) || value<=0.0)return 0;
        }
    *output=staged;
    return 1;
}
