#include "sauti_aac_sbr_hf_joint.h"
#include "sauti_aac_sbr_limits.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define W SAUTI_AAC_SBR_HF_HIGH_BANDS
#define S SAUTI_AAC_SBR_HF_MAX_SLOTS

static void near(double got,double want) {
    assert(isfinite(got) && isfinite(want));
    assert(fabs(got-want)<=2e-5*fmax(1.0,fabs(want)));
}
static void geometry(SautiAacSbrMaster *m,SautiAacSbrTables *t,
                     SautiAacSbrPatches *patches) {
    assert(sauti_aac_sbr_master_linear(16u,32u,1u,0u,m));
    assert(sauti_aac_sbr_tables_build(m,2u,t));
    assert(sauti_aac_sbr_patch_build(48000u,m,t,patches));
}
static void energies(const SautiAacSbrTables *t,
                     SautiAacSbrEnergyFrame *en) {
    memset(en,0,sizeof(*en));
    en->env_count=2u;en->noise_count=2u;
    en->freq_res[0]=0u;en->freq_res[1]=1u;
    for(unsigned i=0;i<t->n_low;i++)en->env_energy[0][i]=4.0;
    for(unsigned i=0;i<t->n_high;i++)en->env_energy[1][i]=4.0;
    for(unsigned n=0;n<t->n_noise;n++){
        en->noise_factor[0][n]=1.0;
        en->noise_factor[1][n]=3.0;
    }
}
static void fill(SautiAacSbrComplex *p,unsigned slots) {
    for(unsigned t=0;t<slots;t++)
        for(unsigned k=0;k<W;k++){
            p[(size_t)t*W+k].re=2.0f;
            p[(size_t)t*W+k].im=0.0f;
        }
}
static SautiAacSbrHfJointConfig config(
    const SautiAacSbrTables *t,
    const SautiAacSbrPatches *patches,
    const SautiAacSbrEnergyFrame *en,
    const uint8_t *flags,
    const uint8_t *env_edges,
    const uint8_t *noise_edges,
    unsigned slots,unsigned transient) {
    SautiAacSbrHfJointConfig c={
        .tables=t,.patches=patches,.energy=en,
        .harmonic_flags=flags,.flag_count=t->n_high,
        .transient_env=transient,.env_edges=env_edges,
        .noise_edges=noise_edges,.slots=slots,
        .limiter_mode=2u,.gain_mode=3u
    };
    return c;
}
static void test_tonal_energy_and_transient(void) {
    SautiAacSbrMaster master;
    SautiAacSbrTables t;
    SautiAacSbrPatches patches;
    geometry(&master,&t,&patches);
    SautiAacSbrEnergyFrame en;
    energies(&t,&en);
    uint8_t flags[SAUTI_AAC_SBR_MAX_MASTER_BANDS]={0};
    flags[0]=1u;flags[2]=1u;
    const unsigned tone=(t.high[0]+t.high[1])/2u;
    assert(tone==17u);
    const uint8_t edges[3]={0,4,8};
    SautiAacSbrHfJointConfig cfg=config(
        &t,&patches,&en,flags,edges,edges,8u,1u);
    SautiAacSbrComplex input[8u*W],out[8u*W+2u];
    fill(input,8u);
    memset(out,0xbc,sizeof(out));
    SautiAacSbrComplex guard[2];
    memcpy(guard,&out[8u*W],sizeof(guard));
    SautiAacSbrHfJointState state={0};
    SautiAacSbrLimiterReport report;
    assert(sauti_aac_sbr_hf_joint_process(
        &cfg,input,8u*W,out,8u*W,&state,&report));
    assert(state.sine.ready==1u && state.sine.phase==0u);
    assert(state.sine.previous[tone]==1u);
    assert(state.noise.ready==1u &&
           state.noise.emitted==(uint64_t)8u*t.m);
    assert(memcmp(guard,&out[8u*W],sizeof(guard))==0);
    assert(report.env_count==2u);
    for(unsigned time=0;time<8u;time++){
        const unsigned k=tone;
        const SautiAacSbrComplex v=out[(size_t)time*W+k];
        if(time>=4u){
            /* 3:1 noise ratio at the tone: signal power 3,
             * sinusoid power 1, zero random noise. */
            const double a=sqrt(3.0);
            const unsigned phase=time&3u;
            near(v.re,a+(phase==0u?1.0:phase==2u?-1.0:0.0));
            near(v.im,phase==1u?-1.0:phase==3u?1.0:0.0);
        }else{
            /* Newly signaled sinusoid starts only at env 1. */
            const double dr=(double)v.re-2.0/sqrt(2.0);
            const double di=v.im;
            near(dr*dr+di*di,2.0);
        }
    }
    /* Untouched frequency range is bitwise preserved. */
    for(unsigned time=0;time<8u;time++)
        for(unsigned k=0;k<W;k++)
            if(k<t.kx || k>=t.kx+t.m){
                const size_t j=(size_t)time*W+k;
                assert(memcmp(&input[j],&out[j],sizeof(out[0]))==0);
            }
    puts("SBR-20: tonal QMF bin has no random noise and reserved 3:1 signal/sine power");
}
static void test_no_tones_matches_legacy_chain(void){
    SautiAacSbrMaster m;
    SautiAacSbrTables t;
    SautiAacSbrPatches p;
    geometry(&m,&t,&p);
    SautiAacSbrEnergyFrame en;
    energies(&t,&en);
    uint8_t flags[SAUTI_AAC_SBR_MAX_MASTER_BANDS]={0};
    const uint8_t edges[3]={0,4,8};
    SautiAacSbrHfJointConfig cfg=config(&t,&p,&en,flags,edges,edges,8,0);
    SautiAacSbrComplex in[8u*W],combined[8u*W],
                       limited[8u*W],noisy[8u*W],manual[8u*W];
    fill(in,8);
    SautiAacSbrHfJointState state={0},reference={0};
    SautiAacSbrLimiterReport stats,refstats;
    assert(sauti_aac_sbr_hf_joint_process(
        &cfg,in,8u*W,combined,8u*W,&state,&stats));
    assert(sauti_aac_sbr_hf_limit_auto(
        &t,&p,2u,&en,edges,8u,3u,in,8u*W,
        limited,8u*W,&refstats));
    assert(sauti_aac_sbr_hf_noise_mix(
        &t,&en,edges,edges,8u,limited,8u*W,
        noisy,8u*W,&reference.noise));
    assert(sauti_aac_sbr_hf_sine_mix(
        &t,&en,flags,t.n_high,0,edges,edges,8u,
        noisy,8u*W,manual,8u*W,&reference.sine));
    assert(memcmp(combined,manual,sizeof(manual))==0);
    assert(memcmp(&state,&reference,sizeof(state))==0);
    assert(memcmp(&stats,&refstats,sizeof(stats))==0);
}
static void test_split_frames_and_alias(void){
    SautiAacSbrMaster m;
    SautiAacSbrTables t;
    SautiAacSbrPatches p;
    geometry(&m,&t,&p);
    SautiAacSbrEnergyFrame en;
    energies(&t,&en);
    uint8_t flags[SAUTI_AAC_SBR_MAX_MASTER_BANDS]={0};
    flags[0]=1u;
    const uint8_t both[3]={0,4,8},half[2]={0,4};
    SautiAacSbrHfJointConfig cfg=config(
        &t,&p,&en,flags,both,both,8u,1u);
    SautiAacSbrComplex in[8u*W],whole[8u*W],
                       split[8u*W],aliased[8u*W];
    fill(in,8);
    SautiAacSbrHfJointState full={0},parts={0},alias_state={0};
    SautiAacSbrLimiterReport report;
    assert(sauti_aac_sbr_hf_joint_process(
        &cfg,in,8u*W,whole,8u*W,&full,&report));
    SautiAacSbrEnergyFrame e1={0};
    e1.env_count=1u;e1.noise_count=1u;
    e1.freq_res[0]=0u;
    memcpy(e1.env_energy[0],en.env_energy[0],
           sizeof(e1.env_energy[0]));
    memcpy(e1.noise_factor[0],en.noise_factor[0],
           sizeof(e1.noise_factor[0]));
    cfg=config(&t,&p,&e1,flags,half,half,4u,1u);
    assert(sauti_aac_sbr_hf_joint_process(
        &cfg,in,4u*W,split,4u*W,&parts,&report));
    e1.freq_res[0]=1u;
    memcpy(e1.env_energy[0],en.env_energy[1],
           sizeof(e1.env_energy[0]));
    memcpy(e1.noise_factor[0],en.noise_factor[1],
           sizeof(e1.noise_factor[0]));
    cfg.transient_env=0u;
    assert(sauti_aac_sbr_hf_joint_process(
        &cfg,in+4u*W,4u*W,split+4u*W,4u*W,&parts,&report));
    assert(memcmp(whole,split,sizeof(whole))==0);
    assert(memcmp(&full,&parts,sizeof(full))==0);
    cfg=config(&t,&p,&en,flags,both,both,8u,1u);
    memcpy(aliased,in,sizeof(aliased));
    assert(sauti_aac_sbr_hf_joint_process(
        &cfg,aliased,8u*W,aliased,8u*W,&alias_state,&report));
    assert(memcmp(whole,aliased,sizeof(whole))==0);
    assert(memcmp(&full,&alias_state,sizeof(full))==0);
    puts("SBR-20: complete and split-frame states match, in-place output safe");
}
static void test_zero_q_is_pure_tone(void) {
    SautiAacSbrMaster m;
    SautiAacSbrTables t;
    SautiAacSbrPatches p;
    geometry(&m,&t,&p);
    SautiAacSbrEnergyFrame en={0};
    en.env_count=1;en.noise_count=1;
    for(unsigned b=0;b<t.n_low;b++)
        en.env_energy[0][b]=4.0;
    uint8_t flags[SAUTI_AAC_SBR_MAX_MASTER_BANDS]={0};
    flags[0]=1u;
    const uint8_t edge[2]={0,4};
    SautiAacSbrHfJointConfig cfg=config(
        &t,&p,&en,flags,edge,edge,4u,0u);
    SautiAacSbrComplex input[4u*W],out[4u*W];
    fill(input,4u);
    SautiAacSbrHfJointState state={0};
    SautiAacSbrLimiterReport stats;
    assert(sauti_aac_sbr_hf_joint_process(
        &cfg,input,4u*W,out,4u*W,&state,&stats));
    const unsigned k=(t.high[0]+t.high[1])/2u;
    for(unsigned time=0;time<4u;time++){
        near(out[(size_t)time*W+k].re,
             time==0?2.0:time==2?-2.0:0.0);
        near(out[(size_t)time*W+k].im,
             time==1?(k&1u?-2.0:2.0):
             time==3?(k&1u?2.0:-2.0):0.0);
    }
    near(out[0u*W+k+1u].re,2.0);
    near(out[0u*W+k+1u].im,0.0);
}
static void test_failure_rolls_back_all_three_outputs(void) {
    SautiAacSbrMaster m;
    SautiAacSbrTables t;
    SautiAacSbrPatches p;
    geometry(&m,&t,&p);
    SautiAacSbrEnergyFrame en;
    energies(&t,&en);
    uint8_t flags[SAUTI_AAC_SBR_MAX_MASTER_BANDS]={0};
    flags[0]=1u;
    uint8_t env[3]={0,4,8},noise[3]={0,4,8};
    SautiAacSbrHfJointConfig cfg=config(
        &t,&p,&en,flags,env,noise,8u,0u);
    SautiAacSbrComplex input[8u*W],output[8u*W],saved[8u*W];
    fill(input,8);
    memset(output,0xa5,sizeof(output));memcpy(saved,output,sizeof(saved));
    SautiAacSbrHfJointState state,prior;
    memset(&state,0,sizeof(state));prior=state;
    SautiAacSbrLimiterReport report,prev_report;
    memset(&report,0xb7,sizeof(report));prev_report=report;
#define REJECT() do { \
    assert(!sauti_aac_sbr_hf_joint_process( \
        &cfg,input,8u*W,output,8u*W,&state,&report)); \
    assert(memcmp(output,saved,sizeof(saved))==0); \
    assert(memcmp(&state,&prior,sizeof(state))==0); \
    assert(memcmp(&report,&prev_report,sizeof(report))==0); \
} while(0)
    cfg.flag_count=t.n_high+1u;REJECT();cfg.flag_count=t.n_high;
    flags[0]=2u;REJECT();flags[0]=1u;
    cfg.gain_mode=4u;REJECT();cfg.gain_mode=3u;
    cfg.limiter_mode=4u;REJECT();cfg.limiter_mode=2u;
    cfg.transient_env=3u;REJECT();cfg.transient_env=0u;
    env[1]=0u;REJECT();env[1]=4u;
    noise[1]=3u;REJECT();noise[1]=4u;
    state.noise.ready=1u;state.noise.seed=0u;prior=state;
    REJECT();
    state.noise.ready=0u;prior=state;
    state.sine.phase=4u;prior=state;
    REJECT(); /* after SBR-16 and SBR-18 succeeded locally */
    state.sine.phase=0u;prior=state;
    state.sine.previous[0]=2u;prior=state;
    REJECT();
    state.sine.previous[0]=0u;prior=state;
    input[7u*W+31u].re=NAN;REJECT();
    input[7u*W+31u].re=2.0f;
    en.noise_factor[1][0]=NAN;REJECT();
    en.noise_factor[1][0]=3.0;
#undef REJECT
}
static void test_40slots_and_sweep(void) {
    SautiAacSbrMaster master;
    SautiAacSbrTables t;
    SautiAacSbrPatches patches;
    geometry(&master,&t,&patches);
    SautiAacSbrEnergyFrame en={0};
    en.env_count=5u;en.noise_count=2u;
    for(unsigned e=0;e<5u;e++){
        en.freq_res[e]=(uint8_t)(e&1u);
        for(unsigned b=0;b<(en.freq_res[e]?t.n_high:t.n_low);b++)
            en.env_energy[e][b]=4.0;
    }
    for(unsigned n=0;n<2u;n++)
        for(unsigned b=0;b<t.n_noise;b++)
            en.noise_factor[n][b]=1.0;
    const uint8_t env[6]={0,8,16,24,32,40},noise[3]={0,24,40};
    uint8_t flags[SAUTI_AAC_SBR_MAX_MASTER_BANDS]={0};
    flags[2]=1u;
    SautiAacSbrHfJointConfig cfg=config(
        &t,&patches,&en,flags,env,noise,40u,2u);
    SautiAacSbrComplex input[S*W],out[S*W];
    fill(input,S);
    SautiAacSbrHfJointState state={0};
    SautiAacSbrLimiterReport report;
    assert(sauti_aac_sbr_hf_joint_process(
        &cfg,input,S*W,out,S*W,&state,&report));
    assert(state.noise.emitted==(uint64_t)S*t.m);
    printf("SBR-20: 40 QMF slots, 5 envelopes, 2 noise intervals PASS\n");

    static const unsigned rates[]={16000u,22050u,24000u,32000u,
                                   44100u,48000u,64000u,88200u,
                                   96000u,128000u,176400u,192000u};
    const uint8_t edges[2]={0,2};
    unsigned done=0u;
    for(unsigned ri=0;ri<sizeof(rates)/sizeof(rates[0]);ri++)
      for(unsigned start=0;start<16u;start+=3u)
        for(unsigned stop=0;stop<16u;stop+=3u){
            SautiAacSbrLimits limits;
            if(!sauti_aac_sbr_limits_compute(
                rates[ri],start,stop,&limits))continue;
            for(unsigned scale=0;scale<4u;scale++)
              for(unsigned alt=0;alt<2u;alt++)
                for(unsigned cross=0;cross<8u;cross++){
                    SautiAacSbrMaster m;
                    const int ok=scale==0u
                      ?sauti_aac_sbr_master_linear(
                          limits.k0,limits.k2,alt,cross,&m)
                      :sauti_aac_sbr_master_log(
                          limits.k0,limits.k2,scale,alt,cross,&m);
                    if(!ok)continue;
                    SautiAacSbrTables tab;
                    if(!sauti_aac_sbr_tables_build(&m,2u,&tab))
                        continue;
                    SautiAacSbrPatches p;
                    if(!sauti_aac_sbr_patch_build(
                        rates[ri],&m,&tab,&p))continue;
                    SautiAacSbrEnergyFrame energy={0};
                    energy.env_count=1u;energy.noise_count=1u;
                    for(unsigned b=0;b<tab.n_low;b++)
                        energy.env_energy[0][b]=4.0;
                    for(unsigned b=0;b<tab.n_noise;b++)
                        energy.noise_factor[0][b]=1.0;
                    uint8_t h[SAUTI_AAC_SBR_MAX_MASTER_BANDS]={0};
                    h[tab.n_high/2u]=1u;
                    SautiAacSbrHfJointConfig c=config(
                        &tab,&p,&energy,h,edges,edges,2u,0u);
                    c.limiter_mode=scale;c.gain_mode=3u;
                    SautiAacSbrComplex two[2u*W],results[2u*W];
                    fill(two,2u);
                    SautiAacSbrHfJointState st={0};
                    assert(sauti_aac_sbr_hf_joint_process(
                        &c,two,2u*W,results,2u*W,&st,&report));
                    done++;
                }
        }
    assert(done>1000u);
    printf("SBR-20 joint chain: %u valid frequency/patch configurations\n",done);
}
int main(void) {
    test_tonal_energy_and_transient();
    test_no_tones_matches_legacy_chain();
    test_split_frames_and_alias();
    test_zero_q_is_pure_tone();
    test_failure_rolls_back_all_three_outputs();
    test_40slots_and_sweep();
    puts("PASS HE-AAC SBR-20 joint HF: limiter, tonal noise suppression, sinusoids, atomic history");
    return 0;
}
