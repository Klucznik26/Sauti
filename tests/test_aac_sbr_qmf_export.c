#include "sauti_aac_sbr_qmf_frame.h"
#include "sauti_aac_sbr_master.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define S 32u
#define Q 64u
static void setup(SautiAacSbrMaster *master,SautiAacSbrTables *tables,
                  SautiAacSbrPatches *patches,
                  SautiAacSbrEnergyFrame *energy){
    assert(sauti_aac_sbr_master_linear(16u,32u,1u,0u,master));
    assert(sauti_aac_sbr_tables_build(master,2u,tables));
    assert(sauti_aac_sbr_patch_build(48000u,master,tables,patches));
    memset(energy,0,sizeof(*energy));
    energy->env_count=1u;energy->noise_count=1u;
    energy->freq_res[0]=1u;
    for(unsigned b=0;b<tables->n_high;b++)
        energy->env_energy[0][b]=(b==tables->n_high-1u)?0.0:16.0;
    for(unsigned b=0;b<tables->n_noise;b++)
        energy->noise_factor[0][b]=0.5;
}
int main(void){
    SautiAacSbrMaster master;
    SautiAacSbrTables tables;
    SautiAacSbrPatches patches;
    SautiAacSbrEnergyFrame energy;
    setup(&master,&tables,&patches,&energy);
    uint8_t flags[SAUTI_AAC_SBR_MAX_MASTER_BANDS]={0};
    const uint8_t edges[2]={0u,32u},attack[1]={0u};
    SautiAacSbrHfJointConfig hf={
        .tables=&tables,.patches=&patches,.energy=&energy,
        .harmonic_flags=flags,.flag_count=tables.n_high,
        .transient_env=0u,.env_edges=edges,.noise_edges=edges,
        .slots=S,.limiter_mode=2u,.gain_mode=3u
    };
    const SautiAacSbrQmfFrameConfig cfg={
        .hf=&hf,.attack=attack,.bs_smoothing_mode=0u,
        .lpc=NULL,.chirp=NULL
    };
    SautiAacSbrQmfFrameState normal={0},exported={0};
    SautiAacSbrLimiterReport nr,er;
    SautiAacSbrQmfStage stage=SAUTI_SBR_QMF_STAGE_INPUT;
    SautiAacSbrComplex qmf[S*Q+1u];
    float core[S*32u],pcm[S*Q],from_qmf[S*Q];
    for(unsigned frame=0;frame<3u;frame++){
        for(unsigned i=0;i<S*32u;i++){
            const int v=(int)((frame*13u+i*23u)%67u)-33;
            core[i]=(float)v*0.125f;
        }
        qmf[S*Q]=(SautiAacSbrComplex){91.0f,92.0f};
        const SautiAacSbrQmfSynthWindowState prior_synthesis=exported.synthesis;
        assert(sauti_aac_sbr_qmf_frame_process(&cfg,core,S*32u,
                                                pcm,S*Q,&normal,&nr));
        assert(sauti_aac_sbr_qmf_frame_export_qmf_ex(&cfg,core,S*32u,
                             qmf,S*Q,&exported,&er,&stage));
        assert(stage==SAUTI_SBR_QMF_STAGE_OK);
        assert(qmf[S*Q].re==91.0f&&qmf[S*Q].im==92.0f);
        assert(memcmp(&nr,&er,sizeof(nr))==0);
        assert(normal.analysis.samples_consumed==exported.analysis.samples_consumed);
        assert(memcmp(&normal.analysis,&exported.analysis,sizeof(normal.analysis))==0);
        assert(memcmp(&normal.hf,&exported.hf,sizeof(normal.hf))==0);
        assert(memcmp(normal.low_prior,exported.low_prior,sizeof(normal.low_prior))==0);
        assert(normal.frames==exported.frames&&normal.ready==exported.ready);
        /* The export must not secretly consume the mono synthesis window. */
        assert(memcmp(&exported.synthesis,&prior_synthesis,sizeof(prior_synthesis))==0);
        assert(sauti_aac_sbr_qmf_synth_64(qmf,S*Q,S,from_qmf,S*Q,
                                          &exported.synthesis));
        assert(memcmp(pcm,from_qmf,sizeof(pcm))==0);
        assert(memcmp(&normal,&exported,sizeof(normal))==0);
        SautiAacSbrQmfFrameState saved=exported;
        SautiAacSbrComplex old[S*Q+1u];
        memcpy(old,qmf,sizeof(old));
        core[S*32u-1u]=NAN;
        assert(!sauti_aac_sbr_qmf_frame_export_qmf_ex(&cfg,core,S*32u,
                           qmf,S*Q,&exported,&er,&stage));
        assert(memcmp(&saved,&exported,sizeof(saved))==0);
        assert(memcmp(old,qmf,sizeof(old))==0);
        core[S*32u-1u]=0.0f;
        assert(!sauti_aac_sbr_qmf_frame_export_qmf_ex(&cfg,core,S*32u,
                           qmf,S*Q-1u,&exported,&er,&stage));
        assert(memcmp(&saved,&exported,sizeof(saved))==0);
        assert(memcmp(old,qmf,sizeof(old))==0);
    }
    puts("PASS AAC PS-4b: native SBR HF QMF export equals mono PCM synthesis, 3 frames, rollback");
    return 0;
}
