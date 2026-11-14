#include "sauti_aac_sbr_hf_noise.h"
#include "sauti_aac_sbr_limits.h"
#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define B SAUTI_AAC_SBR_HF_HIGH_BANDS
#define MAXS SAUTI_AAC_SBR_HF_MAX_SLOTS

static void near(double got,double want) {
    assert(isfinite(got) && isfinite(want));
    assert(fabs(got-want)<=2e-5*fmax(1.0,fabs(want)));
}
static void tables_fixture(SautiAacSbrTables *tables){
    SautiAacSbrMaster m;
    assert(sauti_aac_sbr_master_linear(16,32,1,0,&m));
    assert(sauti_aac_sbr_tables_build(&m,2,tables));
    assert(tables->n_noise==2 && tables->n_high==8);
}
static void energy_fixture(const SautiAacSbrTables *tables,
                           SautiAacSbrEnergyFrame *energy) {
    memset(energy,0,sizeof(*energy));
    energy->env_count=2;
    energy->noise_count=2;
    energy->freq_res[0]=0;
    energy->freq_res[1]=1;
    for(unsigned b=0;b<tables->n_low;b++)
        energy->env_energy[0][b]=4.0;
    for(unsigned b=0;b<tables->n_high;b++)
        energy->env_energy[1][b]=4.0;
    energy->noise_factor[0][0]=1.0;
    energy->noise_factor[0][1]=3.0;
    energy->noise_factor[1][0]=0.25;
    energy->noise_factor[1][1]=9.0;
}
static void fill(SautiAacSbrComplex *input,unsigned slots,float v){
    for(unsigned t=0;t<slots;t++)
        for(unsigned k=0;k<B;k++){
            input[t*B+k].re=v;
            input[t*B+k].im=0.0f;
        }
}
static void test_energy_partition_and_determinism(void){
    SautiAacSbrTables tables;
    tables_fixture(&tables);
    SautiAacSbrEnergyFrame energy;
    energy_fixture(&tables,&energy);
    const uint8_t env[3]={0,4,8},noise[3]={0,4,8};
    SautiAacSbrComplex input[8u*B],out[8u*B+2u],repeat[8u*B];
    fill(input,8,2.0f);
    memset(out,0xaa,sizeof(out));
    SautiAacSbrComplex tail[2];
    memcpy(tail,&out[8u*B],sizeof(tail));
    SautiAacSbrNoiseState state={0},repeat_state={0};
    assert(sauti_aac_sbr_hf_noise_mix(
        &tables,&energy,env,noise,8,input,8u*B,out,8u*B,&state));
    assert(state.ready==1 && state.seed!=0 &&
           state.emitted==8u*tables.m);
    assert(memcmp(tail,&out[8u*B],sizeof(tail))==0);
    assert(sauti_aac_sbr_hf_noise_mix(
        &tables,&energy,env,noise,8,input,8u*B,
        repeat,8u*B,&repeat_state));
    assert(memcmp(out,repeat,sizeof(repeat))==0);
    assert(memcmp(&state,&repeat_state,sizeof(state))==0);
    unsigned changed=0;
    for(unsigned t=0;t<8;t++)
        for(unsigned k=0;k<B;k++){
            const size_t idx=(size_t)t*B+k;
            if(k<tables.kx || k>=tables.kx+tables.m){
                assert(memcmp(&out[idx],&input[idx],
                              sizeof(out[idx]))==0);
                continue;
            }
            const unsigned n=t<4?0u:1u;
            const unsigned band=k<tables.noise[1]?0u:1u;
            const double q=energy.noise_factor[n][band];
            const double signal=2.0/sqrt(1.0+q);
            const double d_re=(double)out[idx].re-signal;
            const double d_im=(double)out[idx].im;
            /* An 8-PSK noise point has |n|=1 exactly: its standalone
             * noise-component power is target*Q/(1+Q). */
            near(d_re*d_re+d_im*d_im,4.0*q/(1.0+q));
            near(signal*signal,4.0/(1.0+q));
            if(fabs(d_im)>0.1)changed++;
        }
    assert(changed>25u);
    puts("SBR-18: exact QMF noise/signal expected energy split, reproducible phases");
}
static void test_state_continuity_and_aliasing(void){
    SautiAacSbrTables tables;
    tables_fixture(&tables);
    SautiAacSbrEnergyFrame energy;
    energy_fixture(&tables,&energy);
    const uint8_t borders[3]={0,4,8},four[2]={0,4};
    SautiAacSbrComplex input[8u*B],whole[8u*B],
                       broken[8u*B],aliased[8u*B];
    fill(input,8u,2.0f);
    SautiAacSbrNoiseState state={0},split={0},alias_state={0};
    assert(sauti_aac_sbr_hf_noise_mix(&tables,&energy,borders,borders,8u,
        input,8u*B,whole,8u*B,&state));

    SautiAacSbrEnergyFrame part={0};
    part.env_count=1;part.noise_count=1;
    part.freq_res[0]=energy.freq_res[0];
    memcpy(part.env_energy[0],energy.env_energy[0],
           sizeof(part.env_energy[0]));
    memcpy(part.noise_factor[0],energy.noise_factor[0],
           sizeof(part.noise_factor[0]));
    assert(sauti_aac_sbr_hf_noise_mix(&tables,&part,four,four,4u,
        input,4u*B,broken,4u*B,&split));
    part.freq_res[0]=energy.freq_res[1];
    memcpy(part.env_energy[0],energy.env_energy[1],
           sizeof(part.env_energy[0]));
    memcpy(part.noise_factor[0],energy.noise_factor[1],
           sizeof(part.noise_factor[0]));
    assert(sauti_aac_sbr_hf_noise_mix(&tables,&part,four,four,4u,
        input+4u*B,4u*B,broken+4u*B,4u*B,&split));
    assert(memcmp(whole,broken,sizeof(whole))==0);
    assert(memcmp(&state,&split,sizeof(state))==0);

    memcpy(aliased,input,sizeof(input));
    assert(sauti_aac_sbr_hf_noise_mix(&tables,&energy,borders,borders,8u,
        aliased,8u*B,aliased,8u*B,&alias_state));
    assert(memcmp(whole,aliased,sizeof(whole))==0);

    /* A distinct per-channel seed yields non-identical noise, avoiding
     * unwanted deterministic L/R correlation. */
    SautiAacSbrNoiseState other={1u,0x12345678u,0u};
    assert(sauti_aac_sbr_hf_noise_mix(&tables,&energy,borders,borders,8u,
        input,8u*B,broken,8u*B,&other));
    assert(memcmp(whole,broken,sizeof(whole))!=0);
}
static void test_real_sbr09_energy_chain(void) {
    SautiAacSbrTables tables;
    tables_fixture(&tables);
    SautiAacSbrQuantFrame quant={0};
    SautiAacSbrGrid grid={0};
    grid.env_count=1;grid.noise_count=1;grid.frame_class=0;
    quant.env_count=1;quant.noise_count=1;
    for(unsigned b=0;b<tables.n_noise;b++)
        quant.noise[0][b]=6u; /* Q=2^(6-6)=1 */
    SautiAacSbrEnergyFrame energy;
    assert(sauti_aac_sbr_energy_dequant(
        &quant,&grid,&tables,1u,0u,&energy));
    near(energy.env_energy[0][0],64.0);
    near(energy.noise_factor[0][0],1.0);
    const uint8_t edges[2]={0,4};
    SautiAacSbrComplex input[4u*B],out[4u*B];
    fill(input,4u,8.0f); /* exact 64 energy before addition */
    SautiAacSbrNoiseState state={0};
    assert(sauti_aac_sbr_hf_noise_mix(&tables,&energy,edges,edges,4u,
        input,4u*B,out,4u*B,&state));
    const double signal=8.0/sqrt(2.0);
    for(unsigned t=0;t<4u;t++)
        for(unsigned k=tables.kx;k<tables.kx+tables.m;k++){
            const double nr=(double)out[t*B+k].re-signal;
            const double ni=(double)out[t*B+k].im;
            near(nr*nr+ni*ni,32.0);
            near(signal*signal,32.0);
        }
}
static void test_zero_target_and_noise_disabled(void) {
    SautiAacSbrTables tables;
    tables_fixture(&tables);
    SautiAacSbrEnergyFrame energy;
    energy_fixture(&tables,&energy);
    const uint8_t borders[3]={0,4,8};
    SautiAacSbrComplex input[8u*B],out[8u*B];
    fill(input,8u,2.0f);
    for(unsigned b=0;b<tables.n_low;b++)
        energy.env_energy[0][b]=0.0;
    for(unsigned b=0;b<tables.n_noise;b++)
        energy.noise_factor[1][b]=0.0;
    SautiAacSbrNoiseState state={0};
    assert(sauti_aac_sbr_hf_noise_mix(&tables,&energy,borders,borders,8u,
        input,8u*B,out,8u*B,&state));
    for(unsigned t=0;t<8;t++)
        for(unsigned k=tables.kx;k<tables.kx+tables.m;k++){
            const SautiAacSbrComplex v=out[t*B+k];
            if(t<4){
                assert(v.re==0.0f && v.im==0.0f);
            }else{
                assert(v.re==2.0f && v.im==0.0f);
            }
        }
}
static void test_fail_closed(void){
    SautiAacSbrTables tables;
    tables_fixture(&tables);
    SautiAacSbrEnergyFrame energy;
    energy_fixture(&tables,&energy);
    uint8_t env[3]={0,4,8},noise[3]={0,4,8};
    SautiAacSbrComplex input[8u*B],out[8u*B],saved[8u*B];
    fill(input,8u,2.0f);
    memset(out,0x88,sizeof(out));memcpy(saved,out,sizeof(out));
    SautiAacSbrNoiseState state={0},before=state;
#define BAD(T,E,A,N,S,I,IC,O,OC,X) do {                              \
    assert(!sauti_aac_sbr_hf_noise_mix((T),(E),(A),(N),(S),          \
                                       (I),(IC),(O),(OC),(X)));      \
    assert(memcmp(saved,out,sizeof(out))==0);                        \
    assert(memcmp(&state,&before,sizeof(state))==0);                 \
} while(0)
    BAD(NULL,&energy,env,noise,8,input,8u*B,out,8u*B,&state);
    BAD(&tables,NULL,env,noise,8,input,8u*B,out,8u*B,&state);
    BAD(&tables,&energy,NULL,noise,8,input,8u*B,out,8u*B,&state);
    BAD(&tables,&energy,env,NULL,8,input,8u*B,out,8u*B,&state);
    BAD(&tables,&energy,env,noise,8,NULL,8u*B,out,8u*B,&state);
    BAD(&tables,&energy,env,noise,8,input,8u*B,NULL,8u*B,&state);
    BAD(&tables,&energy,env,noise,8,input,8u*B,out,8u*B,NULL);
    BAD(&tables,&energy,env,noise,0,input,8u*B,out,8u*B,&state);
    BAD(&tables,&energy,env,noise,MAXS+1u,input,8u*B,out,8u*B,&state);
    BAD(&tables,&energy,env,noise,8,input,8u*B-1u,out,8u*B,&state);
    BAD(&tables,&energy,env,noise,8,input,8u*B,out,8u*B-1u,&state);
    env[1]=0;
    BAD(&tables,&energy,env,noise,8,input,8u*B,out,8u*B,&state);
    env[1]=4;noise[1]=3; /* noise split cannot bisect envelope */
    BAD(&tables,&energy,env,noise,8,input,8u*B,out,8u*B,&state);
    noise[1]=4;noise[2]=7;
    BAD(&tables,&energy,env,noise,8,input,8u*B,out,8u*B,&state);
    noise[2]=8;energy.noise_factor[1][0]=-1.0;
    BAD(&tables,&energy,env,noise,8,input,8u*B,out,8u*B,&state);
    energy.noise_factor[1][0]=NAN;
    BAD(&tables,&energy,env,noise,8,input,8u*B,out,8u*B,&state);
    energy.noise_factor[1][0]=0.25;
    energy.env_energy[1][0]=INFINITY;
    BAD(&tables,&energy,env,noise,8,input,8u*B,out,8u*B,&state);
    energy.env_energy[1][0]=4.0;
    tables.low[1]=17u;
    BAD(&tables,&energy,env,noise,8,input,8u*B,out,8u*B,&state);
    tables_fixture(&tables);
    input[7u*B+31u].im=INFINITY;
    BAD(&tables,&energy,env,noise,8,input,8u*B,out,8u*B,&state);
    input[7u*B+31u].im=0.0f;
    for(unsigned t=0;t<4u;t++)input[t*B+16u].re=0.0f;
    BAD(&tables,&energy,env,noise,8,input,8u*B,out,8u*B,&state);
    for(unsigned t=0;t<4u;t++)input[t*B+16u].re=2.0f;

    state.ready=1u;state.seed=0u;before=state;
    BAD(&tables,&energy,env,noise,8,input,8u*B,out,8u*B,&state);
    state.ready=0u;state.seed=0u;state.emitted=0u;
    state.emitted=UINT64_MAX;before=state;
    BAD(&tables,&energy,env,noise,8,input,8u*B,out,8u*B,&state);
    state=(SautiAacSbrNoiseState){0};
    /* Extremely large target can produce out-of-range FLOAT results. */
    energy.env_energy[0][0]=DBL_MAX;before=state;
    BAD(&tables,&energy,env,noise,8,input,8u*B,out,8u*B,&state);
#undef BAD
}
static void test_frequency_geometry_sweep(void) {
    const unsigned rates[]={16000,22050,24000,32000,44100,48000,
                            64000,88200,96000,128000,176400,192000};
    SautiAacSbrComplex input[2u*B],out[2u*B];
    fill(input,2,2.0f);
    const uint8_t edges[2]={0,2};
    unsigned passed=0;
    for(unsigned r=0;r<sizeof(rates)/sizeof(rates[0]);r++)
      for(unsigned start=0;start<16;start+=3)
        for(unsigned stop=0;stop<16;stop+=3){
            SautiAacSbrLimits lim;
            if(!sauti_aac_sbr_limits_compute(rates[r],start,stop,&lim))
                continue;
            for(unsigned scale=0;scale<4;scale++)
              for(unsigned alter=0;alter<2;alter++)
                for(unsigned cross=0;cross<8;cross++){
                    SautiAacSbrMaster master;
                    const int ok=scale==0
                        ?sauti_aac_sbr_master_linear(
                           lim.k0,lim.k2,alter,cross,&master)
                        :sauti_aac_sbr_master_log(
                           lim.k0,lim.k2,scale,alter,cross,&master);
                    if(!ok)continue;
                    SautiAacSbrTables tables;
                    if(!sauti_aac_sbr_tables_build(&master,2,&tables))
                        continue;
                    SautiAacSbrEnergyFrame energy={0};
                    energy.env_count=1;energy.noise_count=1;
                    for(unsigned b=0;b<tables.n_low;b++)
                        energy.env_energy[0][b]=4.0;
                    for(unsigned b=0;b<tables.n_noise;b++)
                        energy.noise_factor[0][b]=1.0;
                    SautiAacSbrNoiseState state={0};
                    assert(sauti_aac_sbr_hf_noise_mix(
                        &tables,&energy,edges,edges,2u,input,2u*B,
                        out,2u*B,&state));
                    for(unsigned t=0;t<2;t++)
                        for(unsigned k=tables.kx;k<tables.kx+tables.m;k++){
                            const double nr=(double)out[t*B+k].re-
                                            2.0/sqrt(2.0);
                            const double ni=out[t*B+k].im;
                            near(nr*nr+ni*ni,2.0);
                        }
                    passed++;
                }
        }
    assert(passed>1000);
    printf("SBR-18 geometry/noise sweep: %u valid independently tested configurations\n",passed);
}
int main(void) {
    test_energy_partition_and_determinism();
    test_state_continuity_and_aliasing();
    test_real_sbr09_energy_chain();
    test_zero_target_and_noise_disabled();
    test_fail_closed();
    test_frequency_geometry_sweep();
    puts("PASS HE-AAC SBR-18 deterministic HF noise: energy split, two grids, state continuity, rollback");
    return 0;
}
