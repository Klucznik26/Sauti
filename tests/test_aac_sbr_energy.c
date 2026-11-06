#include "sauti_aac_sbr_energy.h"
#include "sauti_aac_sbr_master.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void fixture(SautiAacSbrGrid *g, SautiAacSbrTables *t,
                    SautiAacSbrQuantFrame *q) {
    SautiAacSbrMaster master;
    assert(sauti_aac_sbr_master_linear(16,32,1,0,&master));
    assert(sauti_aac_sbr_tables_build(&master,2,t));
    assert(t->n_high==8u && t->n_low==4u && t->n_noise==2u);
    memset(g,0,sizeof(*g));
    memset(q,0,sizeof(*q));
    g->frame_class=0;g->env_count=2;g->noise_count=2;
    g->freq_res[0]=0;g->freq_res[1]=1;
    q->env_count=2;q->noise_count=2;
    q->freq_res[0]=0;q->freq_res[1]=1;
}

static void close_to(double actual, double expected) {
    assert(isfinite(actual));
    assert(fabs(actual-expected)<=1e-12*fmax(fabs(expected),1.0));
}

static void test_coarse_energy_noise_and_unused(void) {
    SautiAacSbrGrid g;
    SautiAacSbrTables t;
    SautiAacSbrQuantFrame q;
    fixture(&g,&t,&q);
    q.env[0][0]=0; q.env[0][1]=1; q.env[0][2]=2; q.env[0][3]=10;
    q.env[1][0]=3; q.env[1][1]=4; q.env[1][2]=5; q.env[1][3]=6;
    q.env[1][4]=7; q.env[1][5]=8; q.env[1][6]=9; q.env[1][7]=127;
    q.noise[0][0]=0; q.noise[0][1]=6;
    q.noise[1][0]=12; q.noise[1][1]=30;
    SautiAacSbrEnergyFrame out;
    memset(&out,0xcc,sizeof(out));
    assert(sauti_aac_sbr_energy_dequant(&q,&g,&t,1,0,&out));
    assert(out.env_count==2u && out.noise_count==2u);
    assert(out.freq_res[0]==0u && out.freq_res[1]==1u);
    close_to(out.env_energy[0][0],64.0);
    close_to(out.env_energy[0][1],128.0);
    close_to(out.env_energy[0][2],256.0);
    close_to(out.env_energy[0][3],65536.0);
    close_to(out.env_energy[1][7],ldexp(1.0,133));
    close_to(out.noise_factor[0][0],64.0);
    close_to(out.noise_factor[0][1],1.0);
    close_to(out.noise_factor[1][0],1.0/64.0);
    close_to(out.noise_factor[1][1],1.0/16777216.0);
    for(unsigned i=4;i<SAUTI_AAC_SBR_MAX_MASTER_BANDS;i++)
        assert(out.env_energy[0][i]==0.0);
    for(unsigned i=8;i<SAUTI_AAC_SBR_MAX_MASTER_BANDS;i++)
        assert(out.env_energy[1][i]==0.0);
    for(unsigned i=2;i<SAUTI_AAC_SBR_MAX_NOISE_BANDS;i++)
        assert(out.noise_factor[0][i]==0.0);
    for(unsigned e=2;e<SAUTI_AAC_SBR_MAX_ENV;e++)
        for(unsigned b=0;b<SAUTI_AAC_SBR_MAX_MASTER_BANDS;b++)
            assert(out.env_energy[e][b]==0.0);
}

static void test_fine_energy_and_forced_resolution(void) {
    SautiAacSbrGrid g;
    SautiAacSbrTables t;
    SautiAacSbrQuantFrame q;
    fixture(&g,&t,&q);
    q.env[0][0]=0; q.env[0][1]=1; q.env[0][2]=2; q.env[0][3]=3;
    q.env[1][0]=126;q.env[1][1]=127;
    SautiAacSbrEnergyFrame out;
    assert(sauti_aac_sbr_energy_dequant(&q,&g,&t,0,0,&out));
    close_to(out.env_energy[0][0],64.0);
    close_to(out.env_energy[0][1],64.0*sqrt(2.0));
    close_to(out.env_energy[0][2],128.0);
    close_to(out.env_energy[0][3],128.0*sqrt(2.0));
    close_to(out.env_energy[1][0],ldexp(1.0,69));
    close_to(out.env_energy[1][1],ldexp(1.0,69)*sqrt(2.0));

    /* FIXFIX + one time envelope ignores amp_res=1, must use 1.5dB. */
    g.env_count=1;g.noise_count=1;
    q.env_count=1;q.noise_count=1;
    assert(sauti_aac_sbr_energy_dequant(&q,&g,&t,1,0,&out));
    close_to(out.env_energy[0][1],64.0*sqrt(2.0));
    close_to(out.env_energy[0][2],128.0);

    /* A one-envelope VARFIX frame must not force fine resolution. */
    g.frame_class=2;
    assert(sauti_aac_sbr_energy_dequant(&q,&g,&t,1,0,&out));
    close_to(out.env_energy[0][1],128.0);
    close_to(out.env_energy[0][2],256.0);
}

static void test_boundaries_exhaustive(void) {
    SautiAacSbrGrid g;
    SautiAacSbrTables t;
    SautiAacSbrQuantFrame q;
    fixture(&g,&t,&q);
    SautiAacSbrEnergyFrame out;
    g.env_count=q.env_count=1;
    g.noise_count=q.noise_count=1;
    g.frame_class=2;
    unsigned checks=0u;
    for(unsigned amp=0; amp<2; amp++) {
        for(unsigned value=0; value<=127; value++) {
            q.env[0][0]=(uint8_t)value;
            assert(sauti_aac_sbr_energy_dequant(&q,&g,&t,amp,0,&out));
            double x=out.env_energy[0][0];
            close_to(x,64.0*exp2((amp?1.0:0.5)*(double)value));
            if(value>0u){
                double prev=64.0*exp2((amp?1.0:0.5)*(double)(value-1u));
                assert(x>prev);
            }
            ++checks;
        }
    }
    for(unsigned value=0;value<=30;value++){
        q.noise[0][0]=(uint8_t)value;
        assert(sauti_aac_sbr_energy_dequant(&q,&g,&t,1,0,&out));
        close_to(out.noise_factor[0][0],exp2(6.0-(double)value));
        ++checks;
    }
    assert(checks==287u);
    printf("SBR-09: %u exhaustive amplitude/noise quantization checks\n",checks);
}

static void test_odd_band_geometry(void) {
    SautiAacSbrGrid g;
    SautiAacSbrTables t;
    SautiAacSbrQuantFrame q;
    fixture(&g,&t,&q);
    SautiAacSbrMaster m;
    assert(sauti_aac_sbr_master_linear(16,32,1,1,&m));
    assert(sauti_aac_sbr_tables_build(&m,2,&t));
    assert(t.n_high==7u && t.n_low==4u);
    q.freq_res[0]=g.freq_res[0]=1;
    q.freq_res[1]=g.freq_res[1]=0;
    q.env[0][6]=5;
    q.env[1][3]=6;
    SautiAacSbrEnergyFrame out;
    assert(sauti_aac_sbr_energy_dequant(&q,&g,&t,1,0,&out));
    close_to(out.env_energy[0][6],2048.0);
    close_to(out.env_energy[1][3],4096.0);
    assert(out.env_energy[0][7]==0.0);
    assert(out.env_energy[1][4]==0.0);
}

static void test_fail_closed(void) {
    SautiAacSbrGrid g;
    SautiAacSbrTables t;
    SautiAacSbrQuantFrame q;
    fixture(&g,&t,&q);
    SautiAacSbrEnergyFrame out,saved;
    memset(&out,0xa5,sizeof(out));
    saved=out;
#define REJECT(cond) do { \
    assert(!(cond)); \
    assert(memcmp(&out,&saved,sizeof(out))==0); \
} while(0)
    REJECT(sauti_aac_sbr_energy_dequant(NULL,&g,&t,1,0,&out));
    REJECT(sauti_aac_sbr_energy_dequant(&q,NULL,&t,1,0,&out));
    REJECT(sauti_aac_sbr_energy_dequant(&q,&g,NULL,1,0,&out));
    REJECT(sauti_aac_sbr_energy_dequant(&q,&g,&t,1,0,NULL));
    REJECT(sauti_aac_sbr_energy_dequant(&q,&g,&t,2,0,&out));
    REJECT(sauti_aac_sbr_energy_dequant(&q,&g,&t,1,1,&out));
    REJECT(sauti_aac_sbr_energy_dequant(&q,&g,&t,1,2,&out));
    g.frame_class=4u;
    REJECT(sauti_aac_sbr_energy_dequant(&q,&g,&t,1,0,&out));
    g.frame_class=0u;
    q.env_count=1u;
    REJECT(sauti_aac_sbr_energy_dequant(&q,&g,&t,1,0,&out));
    q.env_count=2u;
    q.noise_count=1u;
    REJECT(sauti_aac_sbr_energy_dequant(&q,&g,&t,1,0,&out));
    q.noise_count=2u;
    q.freq_res[1]=2u;
    REJECT(sauti_aac_sbr_energy_dequant(&q,&g,&t,1,0,&out));
    q.freq_res[1]=1u;
    q.env[1][0]=128u;
    REJECT(sauti_aac_sbr_energy_dequant(&q,&g,&t,1,0,&out));
    q.env[1][0]=0u;
    q.noise[1][0]=31u;
    REJECT(sauti_aac_sbr_energy_dequant(&q,&g,&t,1,0,&out));
    q.noise[1][0]=0u;
    t.low[1]=17u; /* 17 is not the middle high boundary 20. */
    REJECT(sauti_aac_sbr_energy_dequant(&q,&g,&t,1,0,&out));
    t.low[1]=20u;
    t.noise[1]=17u; /* Not an actual low-band edge. */
    REJECT(sauti_aac_sbr_energy_dequant(&q,&g,&t,1,0,&out));
    t.noise[1]=24u; /* original noise boundaries in this fixture? */
#undef REJECT
}

typedef struct { uint8_t bytes[16]; size_t pos; } Bits;
static void put(Bits *b, unsigned bits, unsigned value) {
    assert(bits<=32u && b->pos+bits<=sizeof(b->bytes)*8u);
    for(unsigned i=bits;i>0;i--) {
        b->bytes[b->pos/8u] |=
            (uint8_t)(((value>>(i-1u))&1u)<<(7u-b->pos%8u));
        b->pos++;
    }
}
static void test_quant_to_energy_chain(void) {
    /* Independently encoded 3.0 dB envelope and noise data:
     * 0 is a one-bit Huffman symbol in all the chosen time/F books. */
    SautiAacSbrGrid g;
    SautiAacSbrTables t;
    SautiAacSbrQuantFrame q;
    fixture(&g,&t,&q);
    g.freq_res[1]=0u;q.freq_res[1]=0u;
    SautiAacSbrControl ctl;
    memset(&ctl,0,sizeof(ctl));
    ctl.env_count=2;ctl.noise_count=2;ctl.noise_band_count=t.n_noise;
    ctl.df_env[1]=1u;ctl.df_noise[1]=1u;
    Bits b={0};
    put(&b,3,5); const size_t start=b.pos;
    put(&b,6,4);
    for(unsigned j=1;j<t.n_low;j++)put(&b,1,0);
    for(unsigned j=0;j<t.n_low;j++)put(&b,1,0);
    put(&b,5,6);
    for(unsigned j=1;j<t.n_noise;j++)put(&b,1,0);
    for(unsigned j=0;j<t.n_noise;j++)put(&b,1,0);
    const size_t used=(b.pos+7u)/8u;
    SautiAacSbrQuantState history={0};
    size_t cursor=start;
    assert(sauti_aac_sbr_quant_decode(b.bytes,used,&cursor,
                                       &g,&t,&ctl,1,0,&history,&q));
    assert(cursor==b.pos && q.env[0][0]==4u && q.env[1][0]==4u);
    SautiAacSbrEnergyFrame energy;
    assert(sauti_aac_sbr_energy_dequant(&q,&g,&t,1,0,&energy));
    for(unsigned e=0;e<2;e++) {
        for(unsigned j=0;j<t.n_low;j++)close_to(energy.env_energy[e][j],1024.0);
        for(unsigned j=0;j<t.n_noise;j++)close_to(energy.noise_factor[e][j],1.0);
    }
}
int main(void) {
    test_coarse_energy_noise_and_unused();
    test_fine_energy_and_forced_resolution();
    test_boundaries_exhaustive();
    test_odd_band_geometry();
    test_fail_closed();
    test_quant_to_energy_chain();
    puts("PASS HE-AAC SBR-09: linear energies, noise floors, modes, rollback, native quant chain");
    return 0;
}
