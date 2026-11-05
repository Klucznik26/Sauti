/* SBR-54e: audit reveals double allocation of full HF target
 * plus additional noise power. Diagnostic only, no gain changes. */
#include "sauti_aac_sbr_temporal_energy_audit.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define SLOTS 32u
#define BANDS 64u
#define COUNT (SLOTS*BANDS)

static void prepare(SautiAacSbrTemporalHfTargets *targets,
                    SautiAacSbrComplex adjusted[COUNT],
                    SautiAacSbrComplex mixed[COUNT],
                    SautiAacSbrTemporalNoiseFrame *noise,
                    SautiAacSbrTemporalNoiseReport *nr){
    memset(targets,0,sizeof(*targets));
    memset(adjusted,0,COUNT*sizeof(*adjusted));
    memset(mixed,0,COUNT*sizeof(*mixed));
    memset(noise,0,sizeof(*noise));
    memset(nr,0,sizeof(*nr));
    nr->emitted=1u;
    nr->frame_index=5u;
    nr->vectors=SLOTS;
    nr->nonzero_amplitudes=SLOTS;
    for(unsigned t=0u;t<SLOTS;t++){
        targets->slot[t].kx=16u;
        targets->slot[t].k2=17u;
        targets->slot[t].energy[16u]=4.0;
        targets->slot[t].noise_factor[16u]=0.25;
        targets->slot[t].harmonic[16u]=(uint8_t)(t<2u);
        const unsigned ix=t*BANDS+16u;
        noise->active[ix]=1u;
        noise->raw_amplitude[ix]=sqrt(4.0*0.25/1.25);
        adjusted[ix].re=2.0f; /* power 4.0 = FULL E */
        mixed[ix].re=2.0f+(float)noise->raw_amplitude[ix];
    }
}
static void rejected(
    const SautiAacSbrTemporalHfTargets *targets,
    const SautiAacSbrComplex adjusted[COUNT],
    const SautiAacSbrComplex mixed[COUNT],
    const SautiAacSbrTemporalNoiseFrame *noise,
    const SautiAacSbrTemporalApplyReport *apply,
    const SautiAacSbrTemporalNoiseReport *nr,
    SautiAacSbrTemporalEnergyAudit *output){
    const SautiAacSbrTemporalEnergyAudit old=*output;
    assert(!sauti_aac_sbr_temporal_energy_audit(
        targets,adjusted,COUNT,noise,mixed,COUNT,
        apply,nr,output));
    assert(memcmp(&old,output,sizeof(old))==0);
}
int main(void){
    SautiAacSbrTemporalHfTargets targets;
    SautiAacSbrComplex adjusted[COUNT],mixed[COUNT];
    SautiAacSbrTemporalNoiseFrame noise;
    SautiAacSbrTemporalNoiseReport nr;
    SautiAacSbrTemporalApplyReport ar={0};
    SautiAacSbrTemporalEnergyAudit out={0};
    prepare(&targets,adjusted,mixed,&noise,&nr);
    ar.emitted=1u;ar.frame_index=5u;
    assert(sauti_aac_sbr_temporal_energy_audit(
        &targets,adjusted,COUNT,&noise,mixed,COUNT,
        &ar,&nr,&out));
    assert(out.frame_index==5u && out.active_bins==SLOTS &&
           out.double_alloc_bins==SLOTS && out.harmonic_bins==2u);
    assert(fabs(out.decoded_target_power-128.0)<1e-9);
    assert(fabs(out.adjusted_hf_power-128.0)<1e-9);
    assert(fabs(out.nominal_noise_power-25.6)<1e-9);
    assert(fabs(out.max_budget_ratio-1.2)<1e-9);
    puts("SBR-54e: HF full target + raw noise => 1.2x pre-mix budget PASS");

    SautiAacSbrTemporalNoiseReport invalid_report=nr;
    invalid_report.frame_index=6u;
    rejected(&targets,adjusted,mixed,&noise,&ar,&invalid_report,&out);
    invalid_report=nr;
    invalid_report.vectors--;
    rejected(&targets,adjusted,mixed,&noise,&ar,&invalid_report,&out);

    noise.raw_amplitude[31u*BANDS+16u]=NAN;
    rejected(&targets,adjusted,mixed,&noise,&ar,&nr,&out);
    noise.raw_amplitude[31u*BANDS+16u]=sqrt(0.8);
    noise.active[31u*BANDS+16u]=0u;
    rejected(&targets,adjusted,mixed,&noise,&ar,&nr,&out);
    noise.active[31u*BANDS+16u]=1u;
    targets.slot[31u].noise_factor[16u]=0.5;
    rejected(&targets,adjusted,mixed,&noise,&ar,&nr,&out);
    targets.slot[31u].noise_factor[16u]=0.25;
    mixed[31u*BANDS+16u].im=NAN;
    rejected(&targets,adjusted,mixed,&noise,&ar,&nr,&out);
    mixed[31u*BANDS+16u].im=0.0f;
    puts("SBR-54e: last-bin/mismatched-target/NaN rollback PASS");

    /* If HF is not present, noise uses just 20% of E; no double
     * allocation despite the same positive decoded envelope. */
    for(unsigned t=0u;t<SLOTS;t++)
        adjusted[t*BANDS+16u].re=0.0f;
    assert(sauti_aac_sbr_temporal_energy_audit(
        &targets,adjusted,COUNT,&noise,mixed,COUNT,
        &ar,&nr,&out));
    assert(out.double_alloc_bins==0u &&
           out.adjusted_hf_power==0.0 &&
           fabs(out.max_budget_ratio-0.2)<1e-9);
    puts("SBR-54e: noise-only partial budget stays below target PASS");
    puts("PASS SBR-54e decoded SBR target allocation audit (no PCM)");
    return 0;
}
