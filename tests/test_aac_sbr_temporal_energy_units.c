/* SBR-60: decoded envelope-energy unit boundary is exact and transactional. */
#include "sauti_aac_sbr_temporal_energy_units.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

int main(void){
    SautiAacSbrPayloadFrame in={0},out={0},saved={0};
    in.grid.env_count=1u;in.grid.noise_count=1u;
    in.energy.env_count=1u;in.energy.noise_count=1u;
    in.energy.freq_res[0]=0u;
    in.tables.n_low=2u;in.tables.n_high=2u;in.tables.n_noise=1u;
    in.energy.env_energy[0][0]=1073741824.0;
    in.energy.env_energy[0][1]=536870912.0;
    in.energy.noise_factor[0][0]=64.0;
    assert(sauti_aac_sbr_temporal_payload_normalize_qmf_energy(&in,&out));
    assert(out.energy.env_energy[0][0]==1.0);
    assert(out.energy.env_energy[0][1]==0.5);
    assert(out.energy.noise_factor[0][0]==64.0);
    assert(in.energy.env_energy[0][0]==1073741824.0);
    saved=out;
    in.energy.env_energy[0][1]=NAN;
    assert(!sauti_aac_sbr_temporal_payload_normalize_qmf_energy(&in,&out));
    assert(memcmp(&out,&saved,sizeof(out))==0);
    in.energy.env_energy[0][1]=536870912.0;
    in.energy.noise_factor[0][0]=0.0;
    assert(!sauti_aac_sbr_temporal_payload_normalize_qmf_energy(&in,&out));
    assert(memcmp(&out,&saved,sizeof(out))==0);
    puts("PASS SBR-60 normalized QMF envelope-energy boundary");
    return 0;
}
