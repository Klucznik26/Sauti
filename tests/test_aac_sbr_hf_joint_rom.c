#include "sauti_aac_sbr_hf_joint_rom.h"
#include "sauti_aac_sbr_limits.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define W SAUTI_AAC_SBR_HF_HIGH_BANDS
#define MAXS SAUTI_AAC_SBR_HF_MAX_SLOTS

static void close_to(double got,double expected){
    assert(isfinite(got));
    assert(fabs(got-expected)<=3e-5*fmax(1.0,fabs(expected)));
}
static void geometry(SautiAacSbrMaster *m,SautiAacSbrTables *t,
                     SautiAacSbrPatches *p){
    assert(sauti_aac_sbr_master_linear(16u,32u,1u,0u,m));
    assert(sauti_aac_sbr_tables_build(m,2u,t));
    assert(sauti_aac_sbr_patch_build(48000u,m,t,p));
}
static void energy(const SautiAacSbrTables *t,SautiAacSbrEnergyFrame *f,
                   unsigned env_count,double first,double second,
                   double q_first,double q_second){
    memset(f,0,sizeof(*f));
    f->env_count=env_count;
    f->noise_count=env_count==1u?1u:2u;
    for(unsigned e=0;e<env_count;e++){
        f->freq_res[e]=(uint8_t)(e&1u);
        const unsigned n=f->freq_res[e]?t->n_high:t->n_low;
        for(unsigned b=0;b<n;b++)
            f->env_energy[e][b]=e?second:first;
    }
    for(unsigned n=0;n<f->noise_count;n++)
        for(unsigned b=0;b<t->n_noise;b++)
            f->noise_factor[n][b]=n?q_second:q_first;
}
static void fill(SautiAacSbrComplex *p,unsigned slots){
    for(unsigned t=0;t<slots;t++)
        for(unsigned k=0;k<W;k++){
            p[(size_t)t*W+k].re=2.0f;
            p[(size_t)t*W+k].im=0.0f;
        }
}
static SautiAacSbrHfJointConfig config(
    const SautiAacSbrTables *t,const SautiAacSbrPatches *p,
    const SautiAacSbrEnergyFrame *f,const uint8_t *flags,
    const uint8_t *env_edges,const uint8_t *noise_edges,
    unsigned slots,unsigned transient){
    SautiAacSbrHfJointConfig c={
        .tables=t,.patches=p,.energy=f,.harmonic_flags=flags,
        .flag_count=t->n_high,.transient_env=transient,
        .env_edges=env_edges,.noise_edges=noise_edges,.slots=slots,
        .limiter_mode=2u,.gain_mode=3u
    };
    return c;
}
static void test_golden_noise_indices_and_tonal_gate(void) {
    SautiAacSbrMaster master;SautiAacSbrTables t;SautiAacSbrPatches p;
    geometry(&master,&t,&p);
    SautiAacSbrEnergyFrame e;
    energy(&t,&e,2u,4.0,4.0,1.0,3.0);
    uint8_t flags[SAUTI_AAC_SBR_MAX_MASTER_BANDS]={0};
    flags[0]=1u;
    const unsigned harmonic=(t.high[0]+t.high[1])/2u;
    assert(harmonic==17u);
    const uint8_t boundaries[3]={0,4,8},attack[2]={0,0};
    SautiAacSbrHfJointConfig cfg=config(
        &t,&p,&e,flags,boundaries,boundaries,8u,1u);
    SautiAacSbrComplex in[8u*W],out[8u*W+2u];
    fill(in,8u);
    memset(out,0x94,sizeof(out));
    SautiAacSbrComplex guard[2];
    memcpy(guard,&out[8u*W],sizeof(guard));
    SautiAacSbrHfRomState state={0};
    SautiAacSbrLimiterReport stats;
    assert(sauti_aac_sbr_hf_joint_process_rom(
        &cfg,attack,1u,in,8u*W,out,8u*W,&state,&stats));
    assert(state.noise_rom.initialized==1u &&
           state.noise_rom.index==8u*t.m &&
           state.noise_rom.emitted==(uint64_t)8u*t.m);
    assert(state.sine.ready && state.sine.previous[harmonic] &&
           state.sine.phase==0u && state.smooth.ready);
    assert(memcmp(guard,&out[8u*W],sizeof(guard))==0);

    /* MPEG-4 increments the ROM position before lookup.
     * With kx=16 and QMF slot 0, subband 16 -> ROM[1].
     * Complex ROM vectors have non-unit individual magnitude. */
    SautiAacSbrComplex noise_first,noise_second;
    assert(sauti_aac_sbr_noise_rom_lookup(1u,&noise_first));
    assert(sauti_aac_sbr_noise_rom_lookup(2u,&noise_second));
    const double a=sqrt(2.0);
    close_to(out[0u*W+16u].re,a+(double)noise_first.re*a);
    close_to(out[0u*W+16u].im,(double)noise_first.im*a);
    /* Harmonic 17 has not yet started, so it still gets noise. */
    close_to(out[0u*W+harmonic].re,a+(double)noise_second.re*a);
    close_to(out[0u*W+harmonic].im,(double)noise_second.im*a);

    /* At time slot 4 the new harmonic starts. The generator MUST
     * consume ROM[66] but no random noise is mixed in this bin. */
    const SautiAacSbrComplex tone=out[4u*W+harmonic];
    close_to(tone.re,sqrt(3.0)+1.0);
    close_to(tone.im,0.0);
    SautiAacSbrComplex rom_at_next;
    assert(sauti_aac_sbr_noise_rom_lookup(4u*t.m+3u,&rom_at_next));
    close_to(out[4u*W+harmonic+1u].re,
             1.0+(double)rom_at_next.re*sqrt(3.0));
    close_to(out[4u*W+harmonic+1u].im,
             (double)rom_at_next.im*sqrt(3.0));

    for(unsigned time=0;time<8u;time++)
        for(unsigned k=0;k<W;k++)
            if(k<t.kx || k>=t.kx+t.m){
                const size_t ix=(size_t)time*W+k;
                assert(memcmp(&out[ix],&in[ix],sizeof(out[ix]))==0);
            }
    puts("SBR-24: normative ROM[1..128], transient tone suppresses noise without shifting index");
}

static void test_five_tap_and_frame_continuity(void) {
    SautiAacSbrMaster m;SautiAacSbrTables t;SautiAacSbrPatches p;
    geometry(&m,&t,&p);
    SautiAacSbrEnergyFrame full;
    energy(&t,&full,2u,4.0,16.0,0.0,0.0);
    uint8_t flags[SAUTI_AAC_SBR_MAX_MASTER_BANDS]={0};
    const uint8_t edges[3]={0,4,8},half[2]={0,4},attack[2]={0,0};
    const uint8_t attack_half[1]={0};
    SautiAacSbrHfJointConfig cfg=config(&t,&p,&full,
                                         flags,edges,edges,8u,0u);
    SautiAacSbrComplex in[8u*W],whole[8u*W],pieces[8u*W],
                       aliased[8u*W];
    fill(in,8u);
    SautiAacSbrHfRomState single={0},split={0},same={0};
    SautiAacSbrLimiterReport stats;
    assert(sauti_aac_sbr_hf_joint_process_rom(
        &cfg,attack,0u,in,8u*W,whole,8u*W,&single,&stats));
    static const double taps[5]={
        0.33333333333333,0.30150283239582,
        0.21816949906249,0.11516383427084,0.03183050093751
    };
    for(unsigned j=0;j<4u;j++){
        double gain=0;
        for(unsigned i=0;i<5u;i++)
            gain+=taps[i]*(i<=j?2.0:1.0);
        close_to(whole[(size_t)(4u+j)*W+16u].re,2.0*gain);
    }

    SautiAacSbrEnergyFrame first,second;
    energy(&t,&first,1u,4.0,4.0,0.0,0.0);
    energy(&t,&second,1u,16.0,16.0,0.0,0.0);
    second.freq_res[0]=1u;
    for(unsigned i=0;i<t.n_high;i++)
        second.env_energy[0][i]=16.0;

    SautiAacSbrHfJointConfig part=config(&t,&p,&first,
                                         flags,half,half,4u,0u);
    assert(sauti_aac_sbr_hf_joint_process_rom(
        &part,attack_half,0u,in,4u*W,pieces,4u*W,&split,&stats));
    part=config(&t,&p,&second,flags,half,half,4u,0u);
    assert(sauti_aac_sbr_hf_joint_process_rom(
        &part,attack_half,0u,in+4u*W,4u*W,
        pieces+4u*W,4u*W,&split,&stats));
    assert(memcmp(whole,pieces,sizeof(whole))==0);
    assert(memcmp(&single,&split,sizeof(single))==0);

    memcpy(aliased,in,sizeof(in));
    assert(sauti_aac_sbr_hf_joint_process_rom(
        &cfg,attack,0u,aliased,8u*W,aliased,8u*W,&same,&stats));
    assert(memcmp(aliased,whole,sizeof(whole))==0);
    assert(memcmp(&single,&same,sizeof(single))==0);
    puts("SBR-24: five-tap QMF smoothing, frame splitting and in-place output agree");
}

static void test_phase_and_rom_wrap(void) {
    SautiAacSbrMaster m;SautiAacSbrTables t;SautiAacSbrPatches p;
    geometry(&m,&t,&p);
    SautiAacSbrEnergyFrame e;
    energy(&t,&e,1u,4.0,4.0,1.0,1.0);
    uint8_t flags[SAUTI_AAC_SBR_MAX_MASTER_BANDS]={0};
    flags[0]=1u;
    const uint8_t bounds[2]={0,40},attack[1]={0};
    SautiAacSbrHfJointConfig cfg=config(&t,&p,&e,
                                         flags,bounds,bounds,40u,0u);
    SautiAacSbrComplex input[MAXS*W],out[MAXS*W];
    fill(input,MAXS);
    SautiAacSbrHfRomState state={0};
    SautiAacSbrLimiterReport stats;
    assert(sauti_aac_sbr_hf_joint_process_rom(
        &cfg,attack,1u,input,MAXS*W,out,MAXS*W,&state,&stats));
    assert(state.noise_rom.index==((40u*t.m)&511u));
    assert(state.noise_rom.emitted==40u*t.m);
    assert(state.sine.phase==0u && state.sine.previous[17u]==1u);
    /* Four full 40-slot frames cross multiple 512-sample wraps
     * while preserving both sine and smoothing history. */
    for(unsigned n=1;n<4;n++)
        assert(sauti_aac_sbr_hf_joint_process_rom(
            &cfg,attack,1u,input,MAXS*W,out,MAXS*W,&state,&stats));
    assert(state.noise_rom.index==((4u*40u*t.m)&511u));
    assert(state.noise_rom.emitted==4u*40u*t.m);
    puts("SBR-24: four 40-slot frames preserve ROM index modulo 512 and sine phase");
}

static void test_no_noise_is_legacy_equivalent(void) {
    SautiAacSbrMaster m;SautiAacSbrTables t;SautiAacSbrPatches p;
    geometry(&m,&t,&p);
    SautiAacSbrEnergyFrame e;
    energy(&t,&e,2u,4.0,16.0,0.0,0.0);
    uint8_t flags[SAUTI_AAC_SBR_MAX_MASTER_BANDS]={0};
    flags[0]=1u;
    const uint8_t bounds[3]={0,4,8},attack[2]={0,0};
    SautiAacSbrHfJointConfig cfg=config(&t,&p,&e,
                                         flags,bounds,bounds,8u,1u);
    SautiAacSbrComplex in[8u*W],rom_out[8u*W],old_out[8u*W];
    fill(in,8u);
    SautiAacSbrHfRomState rom={0};
    SautiAacSbrHfSmoothState old={0};
    SautiAacSbrLimiterReport rom_report,old_report;
    assert(sauti_aac_sbr_hf_joint_process_rom(
        &cfg,attack,0u,in,8u*W,rom_out,8u*W,&rom,&rom_report));
    assert(sauti_aac_sbr_hf_joint_process_smoothed(
        &cfg,attack,0u,in,8u*W,old_out,8u*W,&old,&old_report));
    assert(memcmp(old_out,rom_out,sizeof(old_out))==0);
    assert(memcmp(&rom.sine,&old.joint.sine,sizeof(rom.sine))==0);
    assert(memcmp(&rom.smooth,&old.smooth,sizeof(rom.smooth))==0);
    assert(memcmp(&rom_report,&old_report,sizeof(rom_report))==0);
    assert(rom.noise_rom.emitted==old.joint.noise.emitted);
    /* Two noise sources differ when noise >0, but mode=0 should match. */
    puts("SBR-24: no-noise 512-ROM path is bit-identical to SBR-22");
}
static void test_fail_closed_at_all_stages(void) {
    SautiAacSbrMaster m;SautiAacSbrTables t;SautiAacSbrPatches p;
    geometry(&m,&t,&p);
    SautiAacSbrEnergyFrame e;
    energy(&t,&e,2u,4.0,16.0,1.0,2.0);
    uint8_t flags[SAUTI_AAC_SBR_MAX_MASTER_BANDS]={0};
    flags[1]=1u;
    uint8_t bounds[3]={0,4,8},attack[2]={0,0};
    SautiAacSbrHfJointConfig cfg=config(&t,&p,&e,
                                         flags,bounds,bounds,8u,0u);
    SautiAacSbrComplex in[8u*W],out[8u*W],old[8u*W];
    fill(in,8);
    memset(out,0x75,sizeof(out));memcpy(old,out,sizeof(out));
    SautiAacSbrHfRomState state={0},before=state;
    SautiAacSbrLimiterReport report,report_before;
    memset(&report,0x94,sizeof(report));report_before=report;
#define FAIL() do { \
    assert(!sauti_aac_sbr_hf_joint_process_rom( \
        &cfg,attack,0u,in,8u*W,out,8u*W,&state,&report)); \
    assert(memcmp(out,old,sizeof(old))==0); \
    assert(memcmp(&state,&before,sizeof(state))==0); \
    assert(memcmp(&report,&report_before,sizeof(report))==0); \
} while(0)
    cfg.limiter_mode=4u;FAIL();cfg.limiter_mode=2u;
    flags[1]=2u;FAIL();flags[1]=1u;
    attack[1]=2u;FAIL();attack[1]=0u;
    bounds[1]=0u;FAIL();bounds[1]=4u;
    e.noise_factor[1][0]=INFINITY;FAIL();e.noise_factor[1][0]=2.0;
    state.noise_rom.initialized=1u;state.noise_rom.index=512u;
    before=state;FAIL(); /* after valid limiter/smoothing, ROM rejects */
    state.noise_rom=(SautiAacSbrNoiseRomState){0};before=state;
    state.noise_rom.emitted=UINT64_MAX;
    before=state;FAIL(); /* overflow guard */
    state.noise_rom=(SautiAacSbrNoiseRomState){0};before=state;
    state.sine.phase=4u;before=state;
    FAIL(); /* sine fails after successful ROM generation */
    state.sine.phase=0u;before=state;
    state.smooth.ready=1u;state.smooth.kx=t.kx;
    state.smooth.m=t.m+1u;before=state;
    FAIL(); /* smoothing fails after limiter */
    state.smooth=(SautiAacSbrSmoothState){0};before=state;
    in[7u*W+31u].re=NAN;FAIL();
#undef FAIL
}
static void test_noise_only_startup_and_strict_limiter(void) {
    SautiAacSbrMaster m;SautiAacSbrTables t;SautiAacSbrPatches p;
    geometry(&m,&t,&p);
    SautiAacSbrEnergyFrame e;
    energy(&t,&e,1u,64.0,64.0,1.0/256.0,1.0/256.0);
    uint8_t flags[SAUTI_AAC_SBR_MAX_MASTER_BANDS]={0};
    const uint8_t bounds[2]={0,8},attack[1]={0};
    SautiAacSbrHfJointConfig cfg=config(
        &t,&p,&e,flags,bounds,bounds,8u,0u);
    SautiAacSbrComplex src[8u*W]={0},out[8u*W];
    SautiAacSbrHfRomState state={0};
    SautiAacSbrLimiterReport report;
    /* Legacy gain-only limiter never invents source HF. */
    SautiAacSbrLimiterBands bands;
    assert(sauti_aac_sbr_limiter_bands_build(
        &t,&p,cfg.limiter_mode,&bands));
    assert(!sauti_aac_sbr_hf_limit(
        &t,&e,bounds,8u,bands.edges,bands.count,cfg.gain_mode,
        src,8u*W,out,8u*W,&report));
    assert(sauti_aac_sbr_hf_joint_process_rom(
        &cfg,attack,1u,src,8u*W,out,8u*W,&state,&report));
    double power=0.0;
    for(unsigned time=0;time<8u;time++)
        for(unsigned k=t.kx;k<t.kx+t.m;k++){
            const size_t ix=(size_t)time*W+k;
            power+=(double)out[ix].re*out[ix].re+
                   (double)out[ix].im*out[ix].im;
            assert(report.applied_gain[0][k]==0.0);
        }
    assert(power>0.0 && state.noise_rom.emitted==8u*t.m);
    /* If noise is absent, zero source still fails and rolls back. */
    const SautiAacSbrHfRomState previous=state;
    e.noise_factor[0][0]=0.0;
    assert(!sauti_aac_sbr_hf_joint_process_rom(
        &cfg,attack,1u,src,8u*W,out,8u*W,&state,&report));
    assert(memcmp(&state,&previous,sizeof(state))==0);
    puts("SBR-46: silent core + signaled ROM noise produces HF; no-noise strict rollback PASS");
}
int main(void) {
    test_golden_noise_indices_and_tonal_gate();
    test_five_tap_and_frame_continuity();
    test_phase_and_rom_wrap();
    test_no_noise_is_legacy_equivalent();
    test_fail_closed_at_all_stages();
    test_noise_only_startup_and_strict_limiter();
    puts("PASS HE-AAC SBR-24 ROM-integrated joint HF: golden, smoothing, wrap, rollback");
    return 0;
}
