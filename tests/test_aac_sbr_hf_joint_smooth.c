#include "sauti_aac_sbr_hf_joint_smooth.h"
#include "sauti_aac_sbr_limits.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define W SAUTI_AAC_SBR_HF_HIGH_BANDS
#define S SAUTI_AAC_SBR_HF_MAX_SLOTS
static void near(double actual,double expected){
    assert(isfinite(actual));
    assert(fabs(actual-expected)<3e-5*fmax(1.0,fabs(expected)));
}
static void setup(unsigned k0,unsigned k2,unsigned rate,
                  SautiAacSbrMaster *m,SautiAacSbrTables *t,
                  SautiAacSbrPatches *p){
    assert(sauti_aac_sbr_master_linear(k0,k2,1u,0u,m));
    assert(sauti_aac_sbr_tables_build(m,2u,t));
    assert(sauti_aac_sbr_patch_build(rate,m,t,p));
}
static void energy(const SautiAacSbrTables *t,
                   SautiAacSbrEnergyFrame *f,
                   unsigned env_count,double p0,double p1,double q){
    memset(f,0,sizeof(*f));
    f->env_count=env_count;
    f->noise_count=env_count==1u?1u:2u;
    for(unsigned e=0;e<env_count;e++){
        f->freq_res[e]=(uint8_t)(e&1u);
        const unsigned bands=f->freq_res[e]?t->n_high:t->n_low;
        for(unsigned b=0;b<bands;b++)
            f->env_energy[e][b]=e?p1:p0;
    }
    for(unsigned n=0;n<f->noise_count;n++)
        for(unsigned b=0;b<t->n_noise;b++)
            f->noise_factor[n][b]=q;
}
static SautiAacSbrHfJointConfig config(
    const SautiAacSbrTables *t,const SautiAacSbrPatches *p,
    const SautiAacSbrEnergyFrame *e,const uint8_t *flags,
    const uint8_t *env_edges,const uint8_t *noise_edges,
    unsigned slots,unsigned transient) {
    SautiAacSbrHfJointConfig c={
        .tables=t,.patches=p,.energy=e,.harmonic_flags=flags,
        .flag_count=t->n_high,.transient_env=transient,
        .env_edges=env_edges,.noise_edges=noise_edges,
        .slots=slots,.limiter_mode=2u,.gain_mode=3u
    };
    return c;
}
static void source_fill(SautiAacSbrComplex *x,unsigned slots){
    for(unsigned s=0;s<slots;s++)
        for(unsigned k=0;k<W;k++){
            x[(size_t)s*W+k].re=2.0f;
            x[(size_t)s*W+k].im=0.0f;
        }
}
static void test_bypass_vs_legacy(void){
    SautiAacSbrMaster m;SautiAacSbrTables t;SautiAacSbrPatches p;
    setup(16,32,48000,&m,&t,&p);
    SautiAacSbrEnergyFrame en;
    energy(&t,&en,2u,4.0,16.0,1.0);
    uint8_t flags[SAUTI_AAC_SBR_MAX_MASTER_BANDS]={0};
    flags[0]=1u;flags[2]=1u;
    const uint8_t edges[3]={0,4,8},attack[2]={0,0};
    SautiAacSbrHfJointConfig c=config(
        &t,&p,&en,flags,edges,edges,8u,1u);
    SautiAacSbrComplex input[8u*W],legacy[8u*W],
                       smoothed[8u*W],alias[8u*W];
    source_fill(input,8u);
    SautiAacSbrHfJointState basic={0};
    SautiAacSbrHfSmoothState advanced={0},same={0};
    SautiAacSbrLimiterReport a,b,c_report;
    assert(sauti_aac_sbr_hf_joint_process(
        &c,input,8u*W,legacy,8u*W,&basic,&a));
    assert(sauti_aac_sbr_hf_joint_process_smoothed(
        &c,attack,1u,input,8u*W,smoothed,8u*W,&advanced,&b));
    for(unsigned j=0;j<8u*W;j++){
        near(smoothed[j].re,legacy[j].re);
        near(smoothed[j].im,legacy[j].im);
    }
    assert(memcmp(&advanced.joint,&basic,sizeof(basic))==0);
    assert(memcmp(&a,&b,sizeof(a))==0);
    assert(advanced.smooth.ready && advanced.smooth.kx==t.kx);
    memcpy(alias,input,sizeof(alias));
    assert(sauti_aac_sbr_hf_joint_process_smoothed(
        &c,attack,1u,alias,8u*W,alias,8u*W,&same,&c_report));
    assert(memcmp(alias,smoothed,sizeof(alias))==0);
    assert(memcmp(&advanced,&same,sizeof(advanced))==0);
    puts("SBR-22: no smoothing matches SBR-20 gain/noise/sine and histories");
}
static void test_gain_ramp_attack_and_history(void){
    SautiAacSbrMaster m;SautiAacSbrTables t;SautiAacSbrPatches p;
    setup(16,32,48000,&m,&t,&p);
    SautiAacSbrEnergyFrame en;
    energy(&t,&en,2u,4.0,16.0,0.0);
    uint8_t flags[SAUTI_AAC_SBR_MAX_MASTER_BANDS]={0};
    const uint8_t env[3]={0,4,8},attacks[2]={0,0};
    SautiAacSbrHfJointConfig c=config(
        &t,&p,&en,flags,env,env,8u,0u);
    SautiAacSbrComplex input[8u*W],out[8u*W];
    source_fill(input,8u);
    SautiAacSbrHfSmoothState state={0};
    SautiAacSbrLimiterReport report;
    assert(sauti_aac_sbr_hf_joint_process_smoothed(
        &c,attacks,0u,input,8u*W,out,8u*W,&state,&report));
    for(unsigned time=0;time<4;time++)
        near(out[(size_t)time*W+16u].re,2.0);
    static const double h[5]={0.33333333333333,0.30150283239582,
                               0.21816949906249,0.11516383427084,
                               0.03183050093751};
    for(unsigned j=0;j<4;j++){
        double g=0;
        for(unsigned z=0;z<5;z++)
            g+=h[z]*(z<=j?2.0:1.0);
        near(out[(size_t)(4u+j)*W+16u].re,2.0*g);
        near(state.smooth.gain[0][16u],j==3u?2.0:
             state.smooth.gain[0][16u]); /* history is finite */
    }
    assert(state.joint.noise.emitted==(uint64_t)8u*t.m);
    assert(state.joint.sine.phase==0u);

    SautiAacSbrHfSmoothState abrupt={0};
    const uint8_t attack_last[2]={0,1};
    assert(sauti_aac_sbr_hf_joint_process_smoothed(
        &c,attack_last,0u,input,8u*W,out,8u*W,&abrupt,&report));
    near(out[4u*W+16u].re,4.0);
    near(out[7u*W+31u].re,4.0);

    /* Single frame and 2+2 envelope split carry IDENTICAL gains/phase
     * histories when frame-level envelopes and attacks are equivalent. */
    SautiAacSbrHfSmoothState split={0},complete={0};
    SautiAacSbrComplex whole[8u*W],pieces[8u*W];
    assert(sauti_aac_sbr_hf_joint_process_smoothed(
        &c,attacks,0u,input,8u*W,whole,8u*W,&complete,&report));
    SautiAacSbrEnergyFrame first={0},second={0};
    energy(&t,&first,1u,4.0,4.0,0.0);
    energy(&t,&second,1u,16.0,16.0,0.0);
    second.freq_res[0]=1u;
    for(unsigned b=0;b<t.n_high;b++)
        second.env_energy[0][b]=16.0;
    const uint8_t edges4[2]={0,4},attack0[1]={0};
    SautiAacSbrHfJointConfig part=config(
        &t,&p,&first,flags,edges4,edges4,4u,0u);
    assert(sauti_aac_sbr_hf_joint_process_smoothed(
        &part,attack0,0u,input,4u*W,pieces,4u*W,&split,&report));
    part=config(&t,&p,&second,flags,edges4,edges4,4u,0u);
    assert(sauti_aac_sbr_hf_joint_process_smoothed(
        &part,attack0,0u,input+4u*W,4u*W,
        pieces+4u*W,4u*W,&split,&report));
    assert(memcmp(whole,pieces,sizeof(whole))==0);
    assert(memcmp(&split,&complete,sizeof(split))==0);
    puts("SBR-22: five-tap gain ramp, attack bypass, 2-frame identity");
}
static void test_noise_amplitude_ramp(void){
    SautiAacSbrMaster m;SautiAacSbrTables t;SautiAacSbrPatches p;
    setup(16,32,48000,&m,&t,&p);
    SautiAacSbrEnergyFrame en;
    energy(&t,&en,2,4.0,4.0,0.0);
    for(unsigned b=0;b<t.n_noise;b++)
        en.noise_factor[1][b]=3.0;
    uint8_t flags[SAUTI_AAC_SBR_MAX_MASTER_BANDS]={0};
    const uint8_t edges[3]={0,4,8},attack[2]={0,0};
    SautiAacSbrHfJointConfig c=config(
        &t,&p,&en,flags,edges,edges,8u,0u);
    SautiAacSbrComplex src[8u*W],out[8u*W];
    source_fill(src,8u);
    SautiAacSbrHfSmoothState state={0};
    SautiAacSbrLimiterReport report;
    assert(sauti_aac_sbr_hf_joint_process_smoothed(
        &c,attack,0u,src,8u*W,out,8u*W,&state,&report));
    /* Noise from env 0 is zero, env 1 should ramp with h[0]. */
    const double noise0=sqrt(4.0*3.0/4.0)*0.33333333333333;
    const double source0=2.0*(0.33333333333333*0.5+
                                (1.0-0.33333333333333)*1.0);
    const SautiAacSbrComplex v=out[4u*W+16u];
    const double delta_re=(double)v.re-source0;
    const double delta_im=v.im;
    near(delta_re*delta_re+delta_im*delta_im,noise0*noise0);
}
static void test_all_or_nothing(void){
    SautiAacSbrMaster m;SautiAacSbrTables t;SautiAacSbrPatches p;
    setup(16,32,48000,&m,&t,&p);
    SautiAacSbrEnergyFrame en;
    energy(&t,&en,2,4.0,16.0,1.0);
    uint8_t flags[SAUTI_AAC_SBR_MAX_MASTER_BANDS]={0};
    flags[0]=1u;
    uint8_t env[3]={0,4,8},attack[2]={0,0};
    SautiAacSbrHfJointConfig c=config(
        &t,&p,&en,flags,env,env,8u,0u);
    SautiAacSbrComplex input[8u*W],output[8u*W],saved[8u*W];
    source_fill(input,8u);
    memset(output,0xa5,sizeof(output));
    memcpy(saved,output,sizeof(saved));
    SautiAacSbrHfSmoothState state={0},previous=state;
    SautiAacSbrLimiterReport report,prior;
    memset(&report,0xb2,sizeof(report));prior=report;
#define REJECT(A,M) do { \
    assert(!sauti_aac_sbr_hf_joint_process_smoothed( \
        &c,(A),(M),input,8u*W,output,8u*W,&state,&report)); \
    assert(memcmp(saved,output,sizeof(saved))==0); \
    assert(memcmp(&state,&previous,sizeof(previous))==0); \
    assert(memcmp(&report,&prior,sizeof(report))==0); \
} while(0)
    REJECT(NULL,0u);
    REJECT(attack,2u);
    attack[1]=2u;REJECT(attack,0u);attack[1]=0u;
    c.limiter_mode=4u;REJECT(attack,0u);c.limiter_mode=2u;
    flags[0]=2u;REJECT(attack,0u);flags[0]=1u;
    env[1]=0u;REJECT(attack,0u);env[1]=4u;
    state.smooth.ready=1u;state.smooth.kx=16u;state.smooth.m=20u;
    previous=state;REJECT(attack,0u);
    state.smooth.ready=0u;state.smooth.kx=0u;state.smooth.m=0u;
    previous=state;
    state.joint.noise.ready=1u;state.joint.noise.seed=0u;
    previous=state;REJECT(attack,0u);
    state.joint.noise.ready=0u;
    previous=state;
    state.joint.sine.phase=4u;
    previous=state;REJECT(attack,0u); /* sine fails after smoothing/noise */
    state.joint.sine.phase=0u;
    previous=state;
    input[7u*W+31u].im=NAN;
    REJECT(attack,0u);
#undef REJECT
}
static void test_40slots_5envelopes(void){
    SautiAacSbrMaster m;SautiAacSbrTables t;SautiAacSbrPatches p;
    setup(16,48,96000,&m,&t,&p);
    SautiAacSbrEnergyFrame en={0};
    en.env_count=5u;en.noise_count=2u;
    for(unsigned e=0;e<5u;e++){
        en.freq_res[e]=(uint8_t)(e&1u);
        const unsigned bands=en.freq_res[e]?t.n_high:t.n_low;
        for(unsigned b=0;b<bands;b++)
            en.env_energy[e][b]=(double)(e+1u)*(e+1u)*4.0;
    }
    for(unsigned n=0;n<2u;n++)
        for(unsigned b=0;b<t.n_noise;b++)
            en.noise_factor[n][b]=n?3.0:1.0;
    uint8_t flags[SAUTI_AAC_SBR_MAX_MASTER_BANDS]={0};
    flags[2u]=1u;
    const uint8_t env[6]={0,8,16,24,32,40},
                  noise[3]={0,24,40},attack[5]={0,0,1,0,0};
    SautiAacSbrHfJointConfig c=config(
        &t,&p,&en,flags,env,noise,40u,2u);
    SautiAacSbrComplex in[S*W],out[S*W];
    source_fill(in,S);
    SautiAacSbrHfSmoothState state={0};
    SautiAacSbrLimiterReport report;
    assert(sauti_aac_sbr_hf_joint_process_smoothed(
        &c,attack,0u,in,S*W,out,S*W,&state,&report));
    assert(state.joint.noise.emitted==(uint64_t)S*t.m);
    assert(state.smooth.ready==1u);
    printf("SBR-22: 40 slots, 5 envelopes, 2 noise regions PASS\n");
}
int main(void){
    test_bypass_vs_legacy();
    test_gain_ramp_attack_and_history();
    test_noise_amplitude_ramp();
    test_all_or_nothing();
    test_40slots_5envelopes();
    puts("PASS HE-AAC SBR-22 joint smoothing: legacy comparison, gain/noise ramp, atomic state");
    return 0;
}
