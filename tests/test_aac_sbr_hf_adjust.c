#include "sauti_aac_sbr_hf_adjust.h"
#include "sauti_aac_sbr_limits.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define NB SAUTI_AAC_SBR_HF_HIGH_BANDS
#define MAXS SAUTI_AAC_SBR_HF_MAX_SLOTS
static void near(double x,double expected) {
    assert(isfinite(x));
    assert(fabs(x-expected) <= 1e-5*fmax(1.0,fabs(expected)));
}
static void init(unsigned k0,unsigned k2,
                 SautiAacSbrMaster *m,
                 SautiAacSbrTables *tables) {
    assert(sauti_aac_sbr_master_linear(k0,k2,1,0,m));
    assert(sauti_aac_sbr_tables_build(m,2,tables));
}
static void init_energy(const SautiAacSbrTables *tables,
                        SautiAacSbrEnergyFrame *energy,
                        unsigned envcount,
                        double level) {
    memset(energy,0,sizeof(*energy));
    energy->env_count=envcount;
    energy->noise_count=envcount==1u?1u:2u;
    for(unsigned e=0;e<envcount;e++){
        energy->freq_res[e]=(uint8_t)(e&1u);
        const unsigned count=energy->freq_res[e]?
                             tables->n_high:tables->n_low;
        for(unsigned b=0;b<count;b++)
            energy->env_energy[e][b]=level;
    }
}
static void fill(SautiAacSbrComplex *x,unsigned slots) {
    for(unsigned s=0;s<slots;s++)
        for(unsigned k=0;k<NB;k++) {
            x[s*NB+k].re=1.0f;
            x[s*NB+k].im=0.0f;
        }
}
static void test_two_resolutions(void) {
    SautiAacSbrMaster m;
    SautiAacSbrTables t;
    init(16,32,&m,&t);
    SautiAacSbrEnergyFrame energy;
    init_energy(&t,&energy,2,16.0);
    for(unsigned b=0;b<t.n_high;b++)energy.env_energy[1][b]=9.0;
    uint8_t border[3]={0,4,8};
    SautiAacSbrComplex low[8u*NB],out[8u*NB+2u];
    fill(low,8);
    for(unsigned s=4;s<8;s++)
        for(unsigned k=t.kx;k<t.kx+t.m;k++)
            low[s*NB+k].re=2.0f;
    memset(out,0xb5,sizeof(out));
    const SautiAacSbrComplex marker[2]={out[8u*NB],out[8u*NB+1u]};
    assert(sauti_aac_sbr_hf_adjust(&t,&energy,border,8,0,
                                   low,8u*NB,out,8u*NB));
    for(unsigned s=0;s<8;s++)
        for(unsigned k=0;k<NB;k++){
            if(k<t.kx || k>=t.kx+t.m) {
                assert(memcmp(&out[s*NB+k],&low[s*NB+k],
                              sizeof(out[0]))==0);
            } else {
                near(out[s*NB+k].re,s<4?4.0:3.0);
                near(out[s*NB+k].im,0.0);
            }
        }
    assert(memcmp(marker,&out[8u*NB],sizeof(marker))==0);
    /* 1.5D? No: here the values are ALREADY linear SBR-09 energy. */
    assert(sauti_aac_sbr_hf_adjust(&t,&energy,border,8,1,
                                   low,8u*NB,out,8u*NB));
    near(out[0*NB+16].re,4.0);
    near(out[7*NB+31].re,3.0);
}

static void test_band_vs_subband(void) {
    SautiAacSbrMaster m;
    SautiAacSbrTables t;
    init(16,32,&m,&t);
    SautiAacSbrEnergyFrame energy;
    init_energy(&t,&energy,1,9.0);
    energy.freq_res[0]=0;
    const unsigned slots=4;
    uint8_t borders[2]={0,4};
    SautiAacSbrComplex input[slots*NB],band[slots*NB],bin[slots*NB];
    fill(input,slots);
    /* Low resolution frequency band widths >1. Alternate amplitudes
     * so band interpolation and per-subband adjustment differ. */
    for(unsigned s=0;s<slots;s++)
        for(unsigned k=t.kx;k<t.kx+t.m;k++)
            input[s*NB+k].re=(k&1u)?2.0f:1.0f;
    assert(sauti_aac_sbr_hf_adjust(&t,&energy,borders,slots,0,
                                   input,slots*NB,band,slots*NB));
    assert(sauti_aac_sbr_hf_adjust(&t,&energy,borders,slots,1,
                                   input,slots*NB,bin,slots*NB));
    for(unsigned b=0;b<t.n_low;b++) {
        const unsigned begin=t.low[b],end=t.low[b+1u];
        double sum0=0,sum1=0;
        for(unsigned s=0;s<slots;s++)
            for(unsigned k=begin;k<end;k++) {
                const double z0=band[s*NB+k].re,z1=bin[s*NB+k].re;
                sum0+=z0*z0;
                sum1+=z1*z1;
                near(z1,3.0);
            }
        near(sum0/(slots*(end-begin)),9.0);
        near(sum1/(slots*(end-begin)),9.0);
    }
    assert(fabs(band[16].re-bin[16].re)>0.2);
    /* input/output alias is legal: source staging is sufficient. */
    assert(sauti_aac_sbr_hf_adjust(&t,&energy,borders,slots,1,
                                   input,slots*NB,input,slots*NB));
    assert(memcmp(input,bin,sizeof(bin))==0);
}

static void test_fail_closed(void) {
    SautiAacSbrMaster m;
    SautiAacSbrTables t;
    init(16,32,&m,&t);
    SautiAacSbrEnergyFrame energy;
    init_energy(&t,&energy,2,16);
    uint8_t borders[3]={0,3,8};
    const unsigned slots=8;
    SautiAacSbrComplex input[slots*NB],output[slots*NB],save[slots*NB];
    fill(input,slots);
    memset(output,0xa5,sizeof(output));
    memcpy(save,output,sizeof(save));
#define REJECT(T,E,B,S,M,I,IC,O,OC) do {                              \
    assert(!sauti_aac_sbr_hf_adjust((T),(E),(B),(S),(M),              \
                                      (I),(IC),(O),(OC)));           \
    assert(memcmp(output,save,sizeof(save))==0);                     \
} while(0)
    REJECT(NULL,&energy,borders,slots,0,input,slots*NB,output,slots*NB);
    REJECT(&t,NULL,borders,slots,0,input,slots*NB,output,slots*NB);
    REJECT(&t,&energy,NULL,slots,0,input,slots*NB,output,slots*NB);
    REJECT(&t,&energy,borders,slots,0,NULL,slots*NB,output,slots*NB);
    REJECT(&t,&energy,borders,slots,0,input,slots*NB,NULL,slots*NB);
    REJECT(&t,&energy,borders,0,0,input,slots*NB,output,slots*NB);
    REJECT(&t,&energy,borders,MAXS+1,0,input,slots*NB,output,slots*NB);
    REJECT(&t,&energy,borders,slots,2,input,slots*NB,output,slots*NB);
    REJECT(&t,&energy,borders,slots,0,input,slots*NB-1,output,slots*NB);
    REJECT(&t,&energy,borders,slots,0,input,slots*NB,output,slots*NB-1);
    borders[1]=0;
    REJECT(&t,&energy,borders,slots,0,input,slots*NB,output,slots*NB);
    borders[1]=3;borders[2]=7;
    REJECT(&t,&energy,borders,slots,0,input,slots*NB,output,slots*NB);
    borders[2]=8;
    energy.env_count=0;
    REJECT(&t,&energy,borders,slots,0,input,slots*NB,output,slots*NB);
    energy.env_count=2;
    energy.noise_count=1;
    REJECT(&t,&energy,borders,slots,0,input,slots*NB,output,slots*NB);
    energy.noise_count=2;
    energy.freq_res[0]=2;
    REJECT(&t,&energy,borders,slots,0,input,slots*NB,output,slots*NB);
    energy.freq_res[0]=0;
    energy.env_energy[1][0]=NAN;
    REJECT(&t,&energy,borders,slots,0,input,slots*NB,output,slots*NB);
    energy.env_energy[1][0]=-1.0;
    REJECT(&t,&energy,borders,slots,0,input,slots*NB,output,slots*NB);
    energy.env_energy[1][0]=16;
    input[7u*NB+31].re=INFINITY;
    REJECT(&t,&energy,borders,slots,0,input,slots*NB,output,slots*NB);
    input[7u*NB+31].re=1;
    input[3u*NB+16].im=NAN;
    REJECT(&t,&energy,borders,slots,1,input,slots*NB,output,slots*NB);
    input[3u*NB+16].im=0;
    t.high[1]=t.high[0];
    REJECT(&t,&energy,borders,slots,0,input,slots*NB,output,slots*NB);
    t.high[1]=m.edges[1];
    /* Impossible to amplify a zero-power envelope without injecting
     * actual HF energy/noise: reject, don't silently 'PASS' with zero. */
    for(unsigned s=0;s<3;s++)
        for(unsigned k=t.kx;k<t.kx+t.m;k++)
            input[s*NB+k].re=0;
    REJECT(&t,&energy,borders,slots,0,input,slots*NB,output,slots*NB);
    /* Nonfinite values outside used high bands are intentionally copied
     * unchanged: they do not affect energy estimation in this stage. */
#undef REJECT
}

static void test_40_slots_and_5_envelopes(void) {
    SautiAacSbrMaster m;
    SautiAacSbrTables t;
    init(16,48,&m,&t);
    SautiAacSbrEnergyFrame energy;
    init_energy(&t,&energy,5,64.0);
    const uint8_t borders[6]={0,8,16,24,32,40};
    SautiAacSbrComplex input[MAXS*NB],out[MAXS*NB];
    fill(input,MAXS);
    assert(sauti_aac_sbr_hf_adjust(&t,&energy,borders,MAXS,0,
                                   input,MAXS*NB,out,MAXS*NB));
    for(unsigned s=0;s<MAXS;s++)
        for(unsigned k=t.kx;k<t.kx+t.m;k++) {
            near(out[s*NB+k].re,8.0);
            near(out[s*NB+k].im,0.0);
        }
    printf("SBR-15: 40 slots, 5 envelopes, 32 adjusted QMF subbands\n");
}

static void test_frequency_sweep(void) {
    static const unsigned rates[]={16000,22050,24000,32000,44100,48000,
                                   64000,88200,96000,128000,176400,192000};
    SautiAacSbrComplex input[2*NB],out[2*NB];
    fill(input,2);
    const uint8_t bounds[3]={0,1,2};
    unsigned passed=0,rejected=0;
    for(unsigned ri=0;ri<sizeof(rates)/sizeof(rates[0]);ri++)
      for(unsigned start=0;start<16;start+=3)
        for(unsigned stop=0;stop<16;stop+=3) {
            SautiAacSbrLimits lim;
            if(!sauti_aac_sbr_limits_compute(rates[ri],start,stop,&lim))
                continue;
            for(unsigned scale=0;scale<4;scale++)
              for(unsigned alter=0;alter<2;alter++)
                for(unsigned cross=0;cross<8;cross++) {
                    SautiAacSbrMaster m;
                    const int ok=scale==0
                      ?sauti_aac_sbr_master_linear(lim.k0,lim.k2,alter,cross,&m)
                      :sauti_aac_sbr_master_log(lim.k0,lim.k2,scale,alter,cross,&m);
                    if(!ok)continue;
                    SautiAacSbrTables t;
                    if(!sauti_aac_sbr_tables_build(&m,2,&t))continue;
                    SautiAacSbrEnergyFrame energy;
                    init_energy(&t,&energy,2,4.0);
                    if(!sauti_aac_sbr_hf_adjust(&t,&energy,bounds,2,scale&1u,
                                                input,2*NB,out,2*NB)) {
                        rejected++;continue;
                    }
                    passed++;
                    for(unsigned s=0;s<2;s++)
                        for(unsigned k=t.kx;k<t.kx+t.m;k++)
                            near(out[s*NB+k].re,2.0);
                }
        }
    assert(passed>1000u && rejected==0u);
    printf("SBR-15 frequency grid sweep: %u energy-matched setups\n",passed);
}
int main(void) {
    test_two_resolutions();
    test_band_vs_subband();
    test_fail_closed();
    test_40_slots_and_5_envelopes();
    test_frequency_sweep();
    puts("PASS HE-AAC SBR-15 amplitude-only QMF envelopes: power matching, 40 slots, rollback");
    return 0;
}
