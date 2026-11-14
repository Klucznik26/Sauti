#include "sauti_aac_sbr_hf_sine.h"
#include "sauti_aac_sbr_limits.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define W SAUTI_AAC_SBR_HF_HIGH_BANDS
#define TMAX SAUTI_AAC_SBR_HF_MAX_SLOTS

static void near(double got,double want) {
    assert(isfinite(got));
    assert(fabs(got-want)<=2e-5*fmax(1.0,fabs(want)));
}
static void fixture(SautiAacSbrTables *tables){
    SautiAacSbrMaster master;
    assert(sauti_aac_sbr_master_linear(16,32,1u,0u,&master));
    assert(sauti_aac_sbr_tables_build(&master,2u,tables));
    assert(tables->n_high==8u && tables->n_noise==2u);
}
static void signal_fill(SautiAacSbrComplex *x,unsigned slots,float re,float im) {
    for(unsigned t=0;t<slots;t++)for(unsigned k=0;k<W;k++){
        x[t*W+k].re=re;
        x[t*W+k].im=im;
    }
}
static void set_energy(const SautiAacSbrTables *tbl,
                       SautiAacSbrEnergyFrame *energy,
                       unsigned count,double e,double q) {
    memset(energy,0,sizeof(*energy));
    energy->env_count=count;
    energy->noise_count=count==1u?1u:2u;
    for(unsigned n=0;n<count;n++){
        energy->freq_res[n]=(uint8_t)(n&1u);
        const unsigned band_count=energy->freq_res[n]?
                                  tbl->n_high:tbl->n_low;
        for(unsigned j=0;j<band_count;j++)
            energy->env_energy[n][j]=e;
    }
    for(unsigned n=0;n<energy->noise_count;n++)
        for(unsigned j=0;j<tbl->n_noise;j++)
            energy->noise_factor[n][j]=q;
}
static unsigned mid(const SautiAacSbrTables *t,unsigned b){
    return (t->high[b]+t->high[b+1u])/2u;
}

static void test_phase_onset_and_previous(void) {
    SautiAacSbrTables tbl;fixture(&tbl);
    SautiAacSbrEnergyFrame eng;set_energy(&tbl,&eng,2,16.0,1.0);
    uint8_t flags[SAUTI_AAC_SBR_MAX_MASTER_BANDS]={0};
    flags[0]=1;flags[2]=1;
    const unsigned k0=mid(&tbl,0),k2=mid(&tbl,2);
    assert(k0==17u && k2==21u);
    const uint8_t edge[3]={0,4,8};
    SautiAacSbrComplex input[8u*W],out[8u*W+2u];
    signal_fill(input,8u,0.25f,-0.5f);
    memset(out,0xcd,sizeof(out));
    SautiAacSbrComplex tail[2];
    memcpy(tail,&out[8u*W],sizeof(tail));
    SautiAacSbrSineState history={0};
    assert(sauti_aac_sbr_hf_sine_mix(
        &tbl,&eng,flags,tbl.n_high,1u,edge,edge,8u,
        input,8u*W,out,8u*W,&history));
    assert(history.ready==1u && history.phase==0u &&
           history.previous[k0]==1u && history.previous[k2]==1u);
    assert(memcmp(tail,&out[8u*W],sizeof(tail))==0);
    const double amp=sqrt(8.0);
    for(unsigned t=0;t<8;t++)
      for(unsigned k=0;k<W;k++) {
        if(t<4u || (k!=k0 && k!=k2)) {
            assert(memcmp(&input[t*W+k],&out[t*W+k],
                          sizeof(input[0]))==0);
            continue;
        }
        const unsigned phase=t&3u;
        const double rr=phase==0u?amp:phase==2u?-amp:0.0;
        const double ii=phase==1u?-amp:phase==3u?amp:0.0;
        near(out[t*W+k].re,0.25+rr);
        near(out[t*W+k].im,-0.5+ii);
    }
    /* All previously active harmonic bins survive an onset gate of
     * env_count (no NEW harmonics), and the phase continues. */
    SautiAacSbrComplex continued[8u*W];
    assert(sauti_aac_sbr_hf_sine_mix(
        &tbl,&eng,flags,tbl.n_high,2u,edge,edge,8u,
        input,8u*W,continued,8u*W,&history));
    near(continued[k0].re,0.25+amp);
    near(continued[k2].re,0.25+amp);
    assert(history.previous[k0] && history.previous[k2]);
    flags[0]=flags[2]=0;
    assert(sauti_aac_sbr_hf_sine_mix(
        &tbl,&eng,flags,tbl.n_high,0u,edge,edge,8u,
        input,8u*W,continued,8u*W,&history));
    assert(memcmp(continued,input,sizeof(continued))==0);
    assert(!history.previous[k0] && !history.previous[k2]);
    puts("SBR-19: four-slot real/imag phase, first-transient onset, frame-history gating");
}

static void test_split_frame_phase_and_alias(void) {
    SautiAacSbrTables tbl;fixture(&tbl);
    SautiAacSbrEnergyFrame e2;set_energy(&tbl,&e2,2u,9.0,0.0);
    SautiAacSbrEnergyFrame e1;set_energy(&tbl,&e1,1u,9.0,0.0);
    uint8_t flags[SAUTI_AAC_SBR_MAX_MASTER_BANDS]={0};
    flags[0]=1;flags[1]=1;flags[2]=1;
    const uint8_t edges2[3]={0,3,7},edges1[2]={0,3},
                  edges2b[2]={0,4};
    SautiAacSbrComplex input[7u*W],whole[7u*W],
                       pieces[7u*W],alias[7u*W];
    signal_fill(input,7u,1.0f,2.0f);
    SautiAacSbrSineState a={0},b={0},c={0};
    assert(sauti_aac_sbr_hf_sine_mix(&tbl,&e2,flags,tbl.n_high,
        0u,edges2,edges2,7u,input,7u*W,whole,7u*W,&a));
    assert(sauti_aac_sbr_hf_sine_mix(&tbl,&e1,flags,tbl.n_high,
        0u,edges1,edges1,3u,input,3u*W,pieces,3u*W,&b));
    assert(sauti_aac_sbr_hf_sine_mix(&tbl,&e1,flags,tbl.n_high,
        1u,edges2b,edges2b,4u,input+3u*W,4u*W,
        pieces+3u*W,4u*W,&b));
    assert(memcmp(whole,pieces,sizeof(whole))==0);
    assert(memcmp(&a,&b,sizeof(a))==0);
    memcpy(alias,input,sizeof(input));
    assert(sauti_aac_sbr_hf_sine_mix(&tbl,&e2,flags,tbl.n_high,
        0u,edges2,edges2,7u,alias,7u*W,alias,7u*W,&c));
    assert(memcmp(alias,whole,sizeof(whole))==0);
    assert(memcmp(&a,&c,sizeof(a))==0);
    assert(a.phase==3u);
}

static void test_sbr09_chain(void) {
    SautiAacSbrTables tbl;fixture(&tbl);
    SautiAacSbrGrid grid={0};
    SautiAacSbrQuantFrame quant={0};
    grid.env_count=1;grid.noise_count=1;grid.frame_class=0;
    grid.freq_res[0]=0;
    quant.env_count=1;quant.noise_count=1;
    quant.freq_res[0]=0;
    for(unsigned b=0;b<tbl.n_noise;b++)
        quant.noise[0][b]=6u; /* Q = 1.0 */
    SautiAacSbrEnergyFrame eng;
    assert(sauti_aac_sbr_energy_dequant(&quant,&grid,&tbl,0u,0u,&eng));
    uint8_t flags[SAUTI_AAC_SBR_MAX_MASTER_BANDS]={0};
    flags[3]=1;
    const uint8_t edge[2]={0,4};
    SautiAacSbrComplex in[4u*W],out[4u*W];
    signal_fill(in,4u,0.0f,0.0f);
    SautiAacSbrSineState state={0};
    assert(sauti_aac_sbr_hf_sine_mix(
        &tbl,&eng,flags,tbl.n_high,0u,edge,edge,4u,
        in,4u*W,out,4u*W,&state));
    const unsigned k=mid(&tbl,3u);
    const double amplitude=sqrt(64.0/2.0);
    near(out[0u*W+k].re,amplitude);
    near(out[1u*W+k].im,(k&1u)?-amplitude:amplitude);
    near(out[2u*W+k].re,-amplitude);
    near(out[3u*W+k].im,(k&1u)?amplitude:-amplitude);
}

static void test_atomic_validation(void) {
    SautiAacSbrTables tbl;fixture(&tbl);
    SautiAacSbrEnergyFrame eng;set_energy(&tbl,&eng,2u,16.0,1.0);
    uint8_t flags[SAUTI_AAC_SBR_MAX_MASTER_BANDS]={0};
    flags[1]=1;
    uint8_t env[3]={0,4,8},nois[3]={0,4,8};
    SautiAacSbrComplex in[8u*W],out[8u*W],saved[8u*W];
    signal_fill(in,8u,1.0f,0.0f);
    memset(out,0x92,sizeof(out));memcpy(saved,out,sizeof(saved));
    SautiAacSbrSineState state={0},original=state;
#define BAD(T,E,F,FC,TR,EN,NO,S,I,IC,O,OC,ST) do {                  \
    assert(!sauti_aac_sbr_hf_sine_mix((T),(E),(F),(FC),(TR),          \
                (EN),(NO),(S),(I),(IC),(O),(OC),(ST)));             \
    assert(memcmp(saved,out,sizeof(out))==0);                       \
    assert(memcmp(&state,&original,sizeof(state))==0);              \
} while (0)
    BAD(NULL,&eng,flags,tbl.n_high,0,env,nois,8,in,8u*W,out,8u*W,&state);
    BAD(&tbl,NULL,flags,tbl.n_high,0,env,nois,8,in,8u*W,out,8u*W,&state);
    BAD(&tbl,&eng,NULL,tbl.n_high,0,env,nois,8,in,8u*W,out,8u*W,&state);
    BAD(&tbl,&eng,flags,tbl.n_high+1u,0,env,nois,8,in,8u*W,out,8u*W,&state);
    BAD(&tbl,&eng,flags,tbl.n_high,3u,env,nois,8,in,8u*W,out,8u*W,&state);
    BAD(&tbl,&eng,flags,tbl.n_high,0,NULL,nois,8,in,8u*W,out,8u*W,&state);
    BAD(&tbl,&eng,flags,tbl.n_high,0,env,NULL,8,in,8u*W,out,8u*W,&state);
    BAD(&tbl,&eng,flags,tbl.n_high,0,env,nois,0,in,8u*W,out,8u*W,&state);
    BAD(&tbl,&eng,flags,tbl.n_high,0,env,nois,TMAX+1u,in,8u*W,out,8u*W,&state);
    BAD(&tbl,&eng,flags,tbl.n_high,0,env,nois,8,NULL,8u*W,out,8u*W,&state);
    BAD(&tbl,&eng,flags,tbl.n_high,0,env,nois,8,in,8u*W-1u,out,8u*W,&state);
    BAD(&tbl,&eng,flags,tbl.n_high,0,env,nois,8,in,8u*W,NULL,8u*W,&state);
    BAD(&tbl,&eng,flags,tbl.n_high,0,env,nois,8,in,8u*W,out,8u*W-1u,&state);
    BAD(&tbl,&eng,flags,tbl.n_high,0,env,nois,8,in,8u*W,out,8u*W,NULL);
    flags[1]=2;
    BAD(&tbl,&eng,flags,tbl.n_high,0,env,nois,8,in,8u*W,out,8u*W,&state);
    flags[1]=1;nois[1]=3;
    BAD(&tbl,&eng,flags,tbl.n_high,0,env,nois,8,in,8u*W,out,8u*W,&state);
    nois[1]=4;eng.env_energy[1][0]=NAN;
    BAD(&tbl,&eng,flags,tbl.n_high,0,env,nois,8,in,8u*W,out,8u*W,&state);
    eng.env_energy[1][0]=16;eng.noise_factor[0][0]=-1.0;
    BAD(&tbl,&eng,flags,tbl.n_high,0,env,nois,8,in,8u*W,out,8u*W,&state);
    eng.noise_factor[0][0]=1.0;tbl.high[1]=tbl.high[0];
    BAD(&tbl,&eng,flags,tbl.n_high,0,env,nois,8,in,8u*W,out,8u*W,&state);
    fixture(&tbl);
    in[7u*W+31u].re=INFINITY;
    BAD(&tbl,&eng,flags,tbl.n_high,0,env,nois,8,in,8u*W,out,8u*W,&state);
    in[7u*W+31u].re=1.0f;
    state.phase=4u;original=state;
    BAD(&tbl,&eng,flags,tbl.n_high,0,env,nois,8,in,8u*W,out,8u*W,&state);
    state.phase=0;state.ready=1u;state.previous[17u]=2u;
    original=state;
    BAD(&tbl,&eng,flags,tbl.n_high,0,env,nois,8,in,8u*W,out,8u*W,&state);
#undef BAD
}
static void test_grid_sweep(void){
    static const unsigned rates[]={16000,22050,24000,32000,44100,
                                   48000,64000,88200,96000,
                                   128000,176400,192000};
    SautiAacSbrComplex in[2u*W],out[2u*W];
    signal_fill(in,2u,0.0f,0.0f);
    const uint8_t edge[2]={0,2};
    unsigned count=0u;
    for(unsigned ri=0;ri<sizeof(rates)/sizeof(rates[0]);ri++)
      for(unsigned start=0;start<16u;start+=3u)
        for(unsigned stop=0;stop<16u;stop+=3u){
            SautiAacSbrLimits lim;
            if(!sauti_aac_sbr_limits_compute(
                rates[ri],start,stop,&lim))continue;
            for(unsigned scale=0;scale<4u;scale++)
              for(unsigned alter=0;alter<2u;alter++)
                for(unsigned cross=0;cross<8u;cross++){
                    SautiAacSbrMaster m;
                    const int ok=scale==0u
                      ? sauti_aac_sbr_master_linear(
                          lim.k0,lim.k2,alter,cross,&m)
                      : sauti_aac_sbr_master_log(
                          lim.k0,lim.k2,scale,alter,cross,&m);
                    if(!ok)continue;
                    SautiAacSbrTables t;
                    if(!sauti_aac_sbr_tables_build(&m,2,&t))continue;
                    uint8_t flags[SAUTI_AAC_SBR_MAX_MASTER_BANDS]={0};
                    flags[t.n_high/2u]=1u;
                    SautiAacSbrEnergyFrame eng={0};
                    eng.env_count=1u;eng.noise_count=1u;
                    for(unsigned b=0;b<t.n_low;b++)
                        eng.env_energy[0][b]=16.0;
                    for(unsigned b=0;b<t.n_noise;b++)
                        eng.noise_factor[0][b]=1.0;
                    SautiAacSbrSineState state={0};
                    assert(sauti_aac_sbr_hf_sine_mix(
                        &t,&eng,flags,t.n_high,0u,edge,edge,2u,
                        in,2u*W,out,2u*W,&state));
                    const unsigned k=mid(&t,t.n_high/2u);
                    near(out[k].re,sqrt(8.0));
                    near(out[W+k].im,(k&1u)?-sqrt(8.0):sqrt(8.0));
                    count++;
                }
        }
    assert(count>1000u);
    printf("SBR-19 harmonic sweep: %u valid QMF band geometries\n",count);
}
int main(void) {
    test_phase_onset_and_previous();
    test_split_frame_phase_and_alias();
    test_sbr09_chain();
    test_atomic_validation();
    test_grid_sweep();
    puts("PASS HE-AAC SBR-19 QMF sinusoids: midpoint bands, 4-phase, transients, history, rollback");
    return 0;
}
