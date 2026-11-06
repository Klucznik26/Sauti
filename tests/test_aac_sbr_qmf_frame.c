#include "sauti_aac_sbr_qmf_frame.h"
#include "sauti_aac_sbr_master.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAXS SAUTI_AAC_SBR_HF_MAX_SLOTS
#define L 32u
#define H 64u

static void setup(SautiAacSbrMaster *master,SautiAacSbrTables *tables,
                  SautiAacSbrPatches *patch){
    assert(sauti_aac_sbr_master_linear(16u,32u,1u,0u,master));
    assert(sauti_aac_sbr_tables_build(master,2u,tables));
    assert(sauti_aac_sbr_patch_build(48000u,master,tables,patch));
    assert(patch->count==1u && patch->width[0]==14u);
    assert(patch->source_for_target[30u]==-1);
}
static void fill(float *pcm,unsigned slots){
    for(unsigned s=0;s<slots;s++)
        for(unsigned i=0;i<L;i++){
            const int v=(int)((s*19u+i*11u)%61u)-30;
            pcm[(size_t)s*L+i]=0.25f*(float)v+
                                ((i&1u)?0.03125f:-0.015625f);
        }
}
static void energy(const SautiAacSbrTables *tables,
                   SautiAacSbrEnergyFrame *e,
                   unsigned envelopes,double power0,double power1,
                   double noise){
    memset(e,0,sizeof(*e));
    e->env_count=envelopes;e->noise_count=envelopes==1u?1u:2u;
    for(unsigned j=0;j<envelopes;j++){
        e->freq_res[j]=1u;
        for(unsigned b=0;b<tables->n_high;b++)
            e->env_energy[j][b]=b==tables->n_high-1u?
                                0.0:(j==0u?power0:power1);
    }
    for(unsigned n=0;n<e->noise_count;n++)
        for(unsigned b=0;b<tables->n_noise;b++)
            e->noise_factor[n][b]=noise;
}
static SautiAacSbrHfJointConfig hf_config(
    const SautiAacSbrTables *tables,
    const SautiAacSbrPatches *patches,
    const SautiAacSbrEnergyFrame *energy,
    const uint8_t *flags,
    const uint8_t *env,
    const uint8_t *noise_borders,unsigned slots,unsigned transient){
    SautiAacSbrHfJointConfig cfg={
        .tables=tables,.patches=patches,.energy=energy,
        .harmonic_flags=flags,.flag_count=tables->n_high,
        .transient_env=transient,.env_edges=env,
        .noise_edges=noise_borders,.slots=slots,
        .limiter_mode=2u,.gain_mode=3u
    };
    return cfg;
}
static SautiAacSbrQmfFrameConfig frame_config(
    const SautiAacSbrHfJointConfig *hf,
    const uint8_t *attack,
    const SautiAacSbrLpcCoeffs *lpc,
    const SautiAacSbrChirp *chirp){
    SautiAacSbrQmfFrameConfig cfg={
        .hf=hf,.attack=attack,.bs_smoothing_mode=0u,
        .lpc=lpc,.chirp=chirp
    };
    return cfg;
}
static void reference_single(const SautiAacSbrQmfFrameConfig *cfg,
                             const float *pcm,
                             float *expected,
                             SautiAacSbrQmfFrameState *state,
                             SautiAacSbrLimiterReport *report){
    const unsigned slots=cfg->hf->slots,kx=cfg->hf->tables->kx;
    const unsigned k2=kx+cfg->hf->tables->m;
    SautiAacSbrComplex low[MAXS*L],high[MAXS*H],
                       adjusted[MAXS*H],mixed[MAXS*H];
    SautiAacSbrComplex lag[(MAXS+2u)*L];
    assert(sauti_aac_sbr_qmf_analyze_32(
        pcm,(size_t)slots*L,slots,L,low,(size_t)slots*L,&state->analysis));
    memcpy(lag,state->low_prior,sizeof(state->low_prior));
    memcpy(lag+2u*L,low,(size_t)slots*L*sizeof(low[0]));
    if(cfg->lpc){
        assert(sauti_aac_sbr_hf_predict(
            cfg->hf->patches,cfg->hf->tables,
            lag,((size_t)slots+2u)*L,slots,
            cfg->lpc,cfg->chirp,high,(size_t)slots*H));
    }else{
        assert(sauti_aac_sbr_hf_transpose(
            cfg->hf->patches,cfg->hf->tables,
            low,(size_t)slots*L,slots,high,(size_t)slots*H));
    }
    assert(sauti_aac_sbr_hf_joint_process_rom(
        cfg->hf,cfg->attack,cfg->bs_smoothing_mode,
        high,(size_t)slots*H,adjusted,(size_t)slots*H,
        &state->hf,report));
    memset(mixed,0,(size_t)slots*H*sizeof(mixed[0]));
    for(unsigned t=0;t<slots;t++) {
        for(unsigned k=0;k<kx;k++)
            mixed[t*H+k]=low[t*L+k];
        for(unsigned k=kx;k<k2;k++)
            mixed[t*H+k]=adjusted[t*H+k];
    }
    assert(sauti_aac_sbr_qmf_synth_64(
        mixed,(size_t)slots*H,slots,expected,(size_t)slots*H,
        &state->synthesis));
    for(unsigned j=0;j<2;j++)
        memcpy(state->low_prior[j],lag+((size_t)slots+j)*L,
               L*sizeof(lag[0]));
    state->ready=1u;state->frames++;
}
static void test_8_slots_manual_vs_frame_and_alias(void){
    SautiAacSbrMaster m;SautiAacSbrTables tables;
    SautiAacSbrPatches patch;setup(&m,&tables,&patch);
    SautiAacSbrEnergyFrame energies;
    energy(&tables,&energies,2u,16.0,36.0,0.5);
    uint8_t flags[SAUTI_AAC_SBR_MAX_MASTER_BANDS]={0};
    flags[2]=1u;
    const uint8_t borders[3]={0,4,8},attacks[2]={0,0};
    SautiAacSbrHfJointConfig hf=hf_config(
        &tables,&patch,&energies,flags,borders,borders,8u,1u);
    SautiAacSbrQmfFrameConfig cfg=frame_config(
        &hf,attacks,NULL,NULL);
    float pcm[8u*L],actual[8u*H+3u],expected[8u*H];
    fill(pcm,8u);
    memset(actual,0x83,sizeof(actual));
    float guard[3];memcpy(guard,actual+8u*H,sizeof(guard));
    SautiAacSbrQmfFrameState state={0},manual={0},alias_state={0};
    SautiAacSbrLimiterReport report,manual_report;
    assert(sauti_aac_sbr_qmf_frame_process(
        &cfg,pcm,8u*L,actual,8u*H,&state,&report));
    reference_single(&cfg,pcm,expected,&manual,&manual_report);
    assert(memcmp(actual,expected,sizeof(expected))==0);
    assert(memcmp(&state,&manual,sizeof(state))==0);
    assert(memcmp(&report,&manual_report,sizeof(report))==0);
    assert(memcmp(guard,actual+8u*H,sizeof(guard))==0);
    assert(state.analysis.samples_consumed==8u*L);
    assert(state.hf.noise_rom.emitted==(uint64_t)8u*tables.m);
    assert(state.synthesis.ready && state.ready && state.frames==1u);
    assert(state.hf.sine.previous[(tables.high[2]+tables.high[3])/2u]);
    float over[8u*H];
    memset(over,0,sizeof(over));
    memcpy(over,pcm,sizeof(pcm));
    assert(sauti_aac_sbr_qmf_frame_process(
        &cfg,over,8u*H,over,8u*H,&alias_state,&report));
    assert(memcmp(over,expected,sizeof(expected))==0);
    assert(memcmp(&state,&alias_state,sizeof(state))==0);
    puts("SBR-28: analysis -> HF ROM -> synthesis matches manually chained modules");
}
static void test_same_frame_vs_two_half_frames(void){
    SautiAacSbrMaster m;SautiAacSbrTables tables;
    SautiAacSbrPatches patch;setup(&m,&tables,&patch);
    SautiAacSbrEnergyFrame full,first,second;
    energy(&tables,&full,2u,16.0,36.0,0.0);
    energy(&tables,&first,1u,16.0,16.0,0.0);
    energy(&tables,&second,1u,36.0,36.0,0.0);
    uint8_t flags[SAUTI_AAC_SBR_MAX_MASTER_BANDS]={0};
    flags[0]=1u;
    const uint8_t both[3]={0,4,8},part[2]={0,4};
    const uint8_t attack2[2]={0,0},attack1[1]={0};
    SautiAacSbrHfJointConfig hf=hf_config(
        &tables,&patch,&full,flags,both,both,8u,1u);
    SautiAacSbrQmfFrameConfig cfg=frame_config(&hf,attack2,NULL,NULL);
    float pcm[8u*L],whole[8u*H],pieces[8u*H];fill(pcm,8u);
    SautiAacSbrQmfFrameState one={0},two={0};
    SautiAacSbrLimiterReport stats;
    assert(sauti_aac_sbr_qmf_frame_process(
        &cfg,pcm,8u*L,whole,8u*H,&one,&stats));
    SautiAacSbrHfJointConfig a=hf_config(
        &tables,&patch,&first,flags,part,part,4u,1u);
    SautiAacSbrQmfFrameConfig c=frame_config(&a,attack1,NULL,NULL);
    assert(sauti_aac_sbr_qmf_frame_process(
        &c,pcm,4u*L,pieces,4u*H,&two,&stats));
    a=hf_config(&tables,&patch,&second,flags,part,part,4u,0u);
    assert(sauti_aac_sbr_qmf_frame_process(
        &c,pcm+4u*L,4u*L,pieces+4u*H,4u*H,&two,&stats));
    assert(memcmp(whole,pieces,sizeof(whole))==0);
    assert(memcmp(&one.analysis,&two.analysis,sizeof(one.analysis))==0);
    assert(memcmp(&one.hf,&two.hf,sizeof(one.hf))==0);
    assert(memcmp(&one.synthesis,&two.synthesis,sizeof(one.synthesis))==0);
    assert(memcmp(one.low_prior,two.low_prior,sizeof(one.low_prior))==0);
    assert(one.frames==1u && two.frames==2u);
    puts("SBR-28: 8 slots and 4+4 slots reproduce identical QMF/PCM and histories");
}
static void test_lpc_chirp_and_empty_frame(void){
    SautiAacSbrMaster m;SautiAacSbrTables tables;
    SautiAacSbrPatches patch;setup(&m,&tables,&patch);
    SautiAacSbrEnergyFrame energies;
    energy(&tables,&energies,1u,9.0,9.0,0.0);
    uint8_t flags[SAUTI_AAC_SBR_MAX_MASTER_BANDS]={0};
    const uint8_t borders[2]={0,4},attack[1]={0};
    SautiAacSbrHfJointConfig hf=hf_config(
        &tables,&patch,&energies,flags,borders,borders,4u,0u);
    SautiAacSbrLpcCoeffs coeff={0};
    for(unsigned k=0;k<L;k++){
        coeff.alpha0[k]=(SautiAacSbrComplex){0.25f,0.125f};
        coeff.alpha1[k]=(SautiAacSbrComplex){-0.0625f,0.125f};
    }
    SautiAacSbrChirp chirp={0};
    uint8_t modes[SAUTI_AAC_SBR_MAX_NOISE_BANDS]={2,1,2,1,0};
    assert(sauti_aac_sbr_chirp_update(
        modes,tables.n_noise,&chirp));
    SautiAacSbrQmfFrameConfig cfg=frame_config(
        &hf,attack,&coeff,&chirp);
    float pcm[4u*L],predicted[4u*H],reference[4u*H];
    fill(pcm,4u);
    SautiAacSbrQmfFrameState state={0},manual={0};
    SautiAacSbrLimiterReport stats,refstats;
    assert(sauti_aac_sbr_qmf_frame_process(
        &cfg,pcm,4u*L,predicted,4u*H,&state,&stats));
    reference_single(&cfg,pcm,reference,&manual,&refstats);
    assert(memcmp(predicted,reference,sizeof(reference))==0);
    assert(memcmp(&state,&manual,sizeof(state))==0);

    /* The same frame without prediction should differ numerically. */
    cfg.lpc=NULL;cfg.chirp=NULL;
    SautiAacSbrQmfFrameState no_prediction={0};
    assert(sauti_aac_sbr_qmf_frame_process(
        &cfg,pcm,4u*L,reference,4u*H,&no_prediction,&stats));
    assert(memcmp(predicted,reference,sizeof(reference))!=0);
    puts("SBR-28: second-order LPC/chirp path is distinct from baseline patching");

    /* A genuine zero-power frame remains silent through every stage. */
    SautiAacSbrQmfFrameState empty={0};
    float silence[4u*L]={0},quiet[4u*H];
    for(unsigned b=0;b<tables.n_high;b++)
        energies.env_energy[0][b]=0.0;
    assert(sauti_aac_sbr_qmf_frame_process(
        &cfg,silence,4u*L,quiet,4u*H,&empty,&stats));
    for(unsigned i=0;i<4u*H;i++)assert(quiet[i]==0.0f);
}
static void test_fail_closed_and_omitted_tail(void){
    SautiAacSbrMaster m;SautiAacSbrTables tables;
    SautiAacSbrPatches patch;setup(&m,&tables,&patch);
    SautiAacSbrEnergyFrame energies;
    energy(&tables,&energies,1u,4.0,4.0,0.0);
    uint8_t flags[SAUTI_AAC_SBR_MAX_MASTER_BANDS]={0};
    const uint8_t borders[2]={0,4};
    uint8_t attack[1]={0};
    SautiAacSbrHfJointConfig hf=hf_config(
        &tables,&patch,&energies,flags,borders,borders,4u,0u);
    SautiAacSbrQmfFrameConfig cfg=frame_config(&hf,attack,NULL,NULL);
    float pcm[4u*L],out[4u*H],saved[4u*H];
    fill(pcm,4u);memset(out,0xa7,sizeof(out));
    memcpy(saved,out,sizeof(out));
    SautiAacSbrQmfFrameState state={0},old=state;
    SautiAacSbrLimiterReport report,old_report;
    memset(&report,0xb9,sizeof(report));old_report=report;
#define REJECT() do { \
    assert(!sauti_aac_sbr_qmf_frame_process( \
        &cfg,pcm,4u*L,out,4u*H,&state,&report)); \
    assert(memcmp(out,saved,sizeof(out))==0); \
    assert(memcmp(&state,&old,sizeof(state))==0); \
    assert(memcmp(&report,&old_report,sizeof(report))==0); \
} while(0)
    /* Last 2 high bins have no source patch. A positive target must
     * reject, rather than claim successful energy reconstruction. */
    energies.env_energy[0][tables.n_high-1u]=4.0;REJECT();
    energies.env_energy[0][tables.n_high-1u]=0.0;
    attack[0]=2u;REJECT();attack[0]=0u;
    hf.gain_mode=4u;REJECT();hf.gain_mode=3u;
    flags[0]=2u;REJECT();flags[0]=0u;
    state.ready=2u;old=state;REJECT();
    state=(SautiAacSbrQmfFrameState){0};old=state;
    state.ready=1u;state.frames=0u;old=state;REJECT();
    state=(SautiAacSbrQmfFrameState){0};old=state;
    state.analysis.ready=1u;state.analysis.history[8]=NAN;old=state;
    REJECT();
    state=(SautiAacSbrQmfFrameState){0};old=state;
    state.hf.noise_rom.initialized=1u;
    state.hf.noise_rom.index=512u;old=state;REJECT();
    state=(SautiAacSbrQmfFrameState){0};old=state;
    state.synthesis.ready=1u;state.synthesis.head=10u;old=state;
    REJECT(); /* final synthesis failure rolls back whole frame */
    state=(SautiAacSbrQmfFrameState){0};old=state;
    pcm[3u*L+31u]=NAN;REJECT();
    pcm[3u*L+31u]=0.0f;
    state.low_prior[1][2u].re=INFINITY;old=state;REJECT();
#undef REJECT
    assert(!sauti_aac_sbr_qmf_frame_process(
        NULL,pcm,4u*L,out,4u*H,&state,&report));
    assert(!sauti_aac_sbr_qmf_frame_process(
        &cfg,NULL,4u*L,out,4u*H,&state,&report));
    assert(!sauti_aac_sbr_qmf_frame_process(
        &cfg,pcm,4u*L,NULL,4u*H,&state,&report));
    puts("SBR-28: unmapped HF target, late NaN, invalid state, final-stage rollback PASS");
}
int main(void){
    test_8_slots_manual_vs_frame_and_alias();
    test_same_frame_vs_two_half_frames();
    test_lpc_chirp_and_empty_frame();
    test_fail_closed_and_omitted_tail();
    puts("PASS HE-AAC SBR-28 full QMF channel integration: analysis, HF, synthesis, history, rollback");
    return 0;
}
