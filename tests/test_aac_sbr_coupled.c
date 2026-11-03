#include "sauti_aac_sbr_energy.h"
#include "sauti_aac_sbr_master.h"
#include "sauti_aac_sbr_quant.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void fixture(SautiAacSbrGrid *grid, SautiAacSbrTables *tables,
                    SautiAacSbrQuantFrame *level,
                    SautiAacSbrQuantFrame *balance) {
    SautiAacSbrMaster master;
    assert(sauti_aac_sbr_master_linear(16,32,1,0,&master));
    assert(sauti_aac_sbr_tables_build(&master,2,tables));
    assert(tables->n_high == 8u && tables->n_low == 4u &&
           tables->n_noise == 2u);
    memset(grid,0,sizeof(*grid));
    grid->frame_class=0;
    grid->env_count=2;
    grid->noise_count=2;
    grid->freq_res[0]=0;
    grid->freq_res[1]=1;
    memset(level,0,sizeof(*level));
    memset(balance,0,sizeof(*balance));
    level->env_count=balance->env_count=2;
    level->noise_count=balance->noise_count=2;
    level->freq_res[0]=balance->freq_res[0]=0;
    level->freq_res[1]=balance->freq_res[1]=1;
}
static void close_to(double got, double want) {
    if (!isfinite(got) || !isfinite(want) ||
        fabs(got - want) > 4e-13 * fmax(fabs(want),1e-10)) {
        fprintf(stderr, "SBR-10 mismatch: got=%.17g expected=%.17g diff=%.17g\\n",
                got, want, got - want);
        assert(0 && "SBR-10 relative tolerance exceeded");
    }
}

static void test_coarse_stereo_and_noise(void) {
    SautiAacSbrGrid grid;
    SautiAacSbrTables tables;
    SautiAacSbrQuantFrame level,balance;
    fixture(&grid,&tables,&level,&balance);
    level.env[0][0]=4; balance.env[0][0]=12;  /* equal pan */
    level.env[0][1]=4; balance.env[0][1]=11;  /* ratio R/L = 2 */
    level.env[1][0]=3; balance.env[1][0]=14;  /* ratio R/L = 1/4 */
    level.noise[0][0]=6; balance.noise[0][0]=12;
    level.noise[0][1]=6; balance.noise[0][1]=11;
    level.noise[1][0]=8; balance.noise[1][0]=14;
    SautiAacSbrEnergyFrame l,r;
    assert(sauti_aac_sbr_coupled_dequant(&level,&balance,&grid,
                                          &tables,1,&l,&r));
    close_to(l.env_energy[0][0],1024.0);
    close_to(r.env_energy[0][0],1024.0);
    close_to(l.env_energy[0][1],2048.0/3.0);
    close_to(r.env_energy[0][1],4096.0/3.0);
    close_to(l.env_energy[1][0],819.2);
    close_to(r.env_energy[1][0],204.8);
    close_to(l.noise_factor[0][0],1.0);
    close_to(r.noise_factor[0][0],1.0);
    close_to(l.noise_factor[0][1],2.0/3.0);
    close_to(r.noise_factor[0][1],4.0/3.0);
    close_to(l.noise_factor[1][0],0.4);
    close_to(r.noise_factor[1][0],0.1);
    for(unsigned b=tables.n_low;b<SAUTI_AAC_SBR_MAX_MASTER_BANDS;b++)
        assert(l.env_energy[0][b]==0.0 && r.env_energy[0][b]==0.0);
}

static void test_fine_and_forced_resolution(void) {
    SautiAacSbrGrid grid;
    SautiAacSbrTables tables;
    SautiAacSbrQuantFrame level,balance;
    fixture(&grid,&tables,&level,&balance);
    level.env[0][0]=2;balance.env[0][0]=24;
    level.env[1][0]=3;balance.env[1][0]=22;
    SautiAacSbrEnergyFrame l,r;
    assert(sauti_aac_sbr_coupled_dequant(&level,&balance,&grid,
                                          &tables,0,&l,&r));
    close_to(l.env_energy[0][0],128.0);
    close_to(r.env_energy[0][0],128.0);
    /* Difference of 2 quantized balance steps at 1.5 dB is one octave. */
    const double total=256.0*sqrt(2.0),ratio=2.0;
    close_to(l.env_energy[1][0],total/(1.0+ratio));
    close_to(r.env_energy[1][0],total*ratio/(1.0+ratio));
    /* FIXFIX with only one envelope MUST force fine amp resolution. */
    grid.env_count=level.env_count=balance.env_count=1;
    grid.noise_count=level.noise_count=balance.noise_count=1;
    assert(sauti_aac_sbr_coupled_dequant(&level,&balance,&grid,
                                          &tables,1,&l,&r));
    close_to(l.env_energy[0][0],128.0);
    close_to(r.env_energy[0][0],128.0);
    grid.frame_class=2; /* VARFIX with one env uses normal coarse mode */
    assert(sauti_aac_sbr_coupled_dequant(&level,&balance,&grid,
                                          &tables,1,&l,&r));
    /* q_balance 24 instead of center 12 makes left > right. */
    assert(l.env_energy[0][0]>r.env_energy[0][0]);
    close_to(l.env_energy[0][0]+r.env_energy[0][0],512.0);
}

static void test_all_extremes_conservation(void) {
    SautiAacSbrGrid grid;
    SautiAacSbrTables tables;
    SautiAacSbrQuantFrame level,balance;
    fixture(&grid,&tables,&level,&balance);
    SautiAacSbrEnergyFrame l,r,reference;
    unsigned checks=0;
    for(unsigned amp=0;amp<2;amp++) {
        for(unsigned q=0;q<128;q++) {
            for(unsigned pan=0;pan<128;pan++) {
                level.env[0][0]=(uint8_t)q;
                balance.env[0][0]=(uint8_t)pan;
                assert(sauti_aac_sbr_coupled_dequant(&level,&balance,
                                            &grid,&tables,amp,&l,&r));
                assert(sauti_aac_sbr_energy_dequant(&level,&grid,
                                                    &tables,amp,0,&reference));
                const double one=reference.env_energy[0][0];
                close_to(l.env_energy[0][0]+r.env_energy[0][0],2.0*one);
                double offset=amp?12.0:24.0,alpha=amp?1.0:0.5;
                close_to(r.env_energy[0][0]/l.env_energy[0][0],
                         exp2(alpha*(offset-(double)pan)));
                checks++;
            }
        }
    }
    for(unsigned q=0;q<=30;q++) {
        level.noise[0][0]=(uint8_t)q;
        for(unsigned pan=0;pan<=30;pan++) {
            balance.noise[0][0]=(uint8_t)pan;
            assert(sauti_aac_sbr_coupled_dequant(&level,&balance,
                                        &grid,&tables,1,&l,&r));
            close_to(l.noise_factor[0][0]+r.noise_factor[0][0],
                     exp2(7.0-(double)q));
            close_to(r.noise_factor[0][0]/l.noise_factor[0][0],
                     exp2(12.0-(double)pan));
            checks++;
        }
    }
    assert(checks==33729u);
    printf("SBR-10: %u independent energy/noise conservation and pan checks\n",
           checks);
}

static void test_errors_keep_both_outputs(void) {
    SautiAacSbrGrid grid;
    SautiAacSbrTables tables;
    SautiAacSbrQuantFrame level,balance;
    fixture(&grid,&tables,&level,&balance);
    SautiAacSbrEnergyFrame l,r,oldl,oldr;
    memset(&l,0xa5,sizeof(l));
    memset(&r,0x5a,sizeof(r));
    oldl=l;oldr=r;
#define REJECT(...) do { \
    assert(!sauti_aac_sbr_coupled_dequant(__VA_ARGS__)); \
    assert(memcmp(&l,&oldl,sizeof(l))==0); \
    assert(memcmp(&r,&oldr,sizeof(r))==0); \
} while(0)
    REJECT(NULL,&balance,&grid,&tables,1,&l,&r);
    REJECT(&level,NULL,&grid,&tables,1,&l,&r);
    REJECT(&level,&balance,NULL,&tables,1,&l,&r);
    REJECT(&level,&balance,&grid,NULL,1,&l,&r);
    REJECT(&level,&balance,&grid,&tables,2,&l,&r);
    REJECT(&level,&balance,&grid,&tables,1,NULL,&r);
    REJECT(&level,&balance,&grid,&tables,1,&l,NULL);
    REJECT(&level,&balance,&grid,&tables,1,&l,&l);
    REJECT(&level,&level,&grid,&tables,1,&l,&r);
    level.env_count=0;
    REJECT(&level,&balance,&grid,&tables,1,&l,&r);
    level.env_count=2;
    balance.noise_count=1;
    REJECT(&level,&balance,&grid,&tables,1,&l,&r);
    balance.noise_count=2;
    balance.freq_res[1]=0;
    REJECT(&level,&balance,&grid,&tables,1,&l,&r);
    balance.freq_res[1]=1;
    balance.env[1][7]=128;
    REJECT(&level,&balance,&grid,&tables,1,&l,&r);
    balance.env[1][7]=0;
    level.noise[0][0]=31;
    REJECT(&level,&balance,&grid,&tables,1,&l,&r);
    level.noise[0][0]=0;
    grid.freq_res[1]=2;
    REJECT(&level,&balance,&grid,&tables,1,&l,&r);
    grid.freq_res[1]=1;
    tables.high[1]=16; /* duplicate boundary */
    REJECT(&level,&balance,&grid,&tables,1,&l,&r);
#undef REJECT
}

typedef struct {uint8_t bytes[32];size_t pos;} Bits;
static void put(Bits *b,unsigned bits,unsigned value) {
    assert(bits<=16u && b->pos+bits<=sizeof(b->bytes)*8u);
    for(unsigned k=bits;k>0;k--) {
        b->bytes[b->pos/8u] |=
            (uint8_t)(((value>>(k-1u))&1u)<<(7u-b->pos%8u));
        b->pos++;
    }
}
static void test_native_sbr08_to_coupled_energy(void) {
    /* Construct separate genuinely Huffman-coded SBR level/balance
     * streams then decode through SBR-08 before paired unmapping.
     * For 3dB, zero delta uses one bit '0' in every chosen VLC. */
    SautiAacSbrGrid grid;
    SautiAacSbrTables tables;
    SautiAacSbrQuantFrame level,balance;
    fixture(&grid,&tables,&level,&balance);
    grid.freq_res[1]=0;
    SautiAacSbrControl c={0};
    c.env_count=2;
    c.noise_count=2;
    c.noise_band_count=tables.n_noise;
    Bits lbits={0},rbits={0};
    for(unsigned e=0;e<2;e++) {
        put(&lbits,6,4);  /* quantized combined envelope */
        put(&rbits,5,6);  /* balanced start doubled by SBR-08 -> 12 */
        for(unsigned j=1;j<tables.n_low;j++) {
            put(&lbits,1,0);
            put(&rbits,1,0);
        }
    }
    for(unsigned e=0;e<2;e++) {
        put(&lbits,5,6);  /* quantized level noise */
        put(&rbits,5,6);  /* balanced noise -> 12 */
        for(unsigned j=1;j<tables.n_noise;j++){
            put(&lbits,1,0);
            put(&rbits,1,0);
        }
    }
    SautiAacSbrQuantState ls={0},rs={0};
    size_t lp=0,rp=0;
    assert(sauti_aac_sbr_quant_decode(lbits.bytes,(lbits.pos+7u)/8u,
                                      &lp,&grid,&tables,&c,1,0,&ls,&level));
    assert(sauti_aac_sbr_quant_decode(rbits.bytes,(rbits.pos+7u)/8u,
                                      &rp,&grid,&tables,&c,1,1,&rs,&balance));
    assert(lp==lbits.pos && rp==rbits.pos);
    assert(balance.env[0][0]==12 && balance.noise[0][0]==12);
    SautiAacSbrEnergyFrame left,right;
    assert(sauti_aac_sbr_coupled_dequant(&level,&balance,
                                          &grid,&tables,1,&left,&right));
    for(unsigned e=0;e<2;e++) {
        for(unsigned j=0;j<tables.n_low;j++) {
            close_to(left.env_energy[e][j],1024.0);
            close_to(right.env_energy[e][j],1024.0);
        }
        for(unsigned j=0;j<tables.n_noise;j++) {
            close_to(left.noise_factor[e][j],1.0);
            close_to(right.noise_factor[e][j],1.0);
        }
    }
}

int main(void) {
    test_coarse_stereo_and_noise();
    test_fine_and_forced_resolution();
    test_all_extremes_conservation();
    test_errors_keep_both_outputs();
    test_native_sbr08_to_coupled_energy();
    puts("PASS HE-AAC SBR-10 coupled stereo: pan/unmapping, all ranges, atomicity, native SBR-08");
    return 0;
}
