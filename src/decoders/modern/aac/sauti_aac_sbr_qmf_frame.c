/* HE-AAC SBR-28 — first full QMF channel integration.
 * All stages are independently tested SBR-12/13, 24, 27, 25/26.
 * The transactional frame driver uses caller-supplied SBR side data.
 * Neither this module nor its dependencies parse/decode HE-AAC frames.
 */
#include "sauti_aac_sbr_qmf_frame.h"
#include <math.h>
#include <string.h>

static int qmf_frame_process_impl(
    const SautiAacSbrQmfFrameConfig *config,
    const float *core_pcm,
    size_t core_count,
    float *output,
    size_t output_count,
    SautiAacSbrComplex *qmf_output,size_t qmf_count,
    unsigned export_qmf,
    SautiAacSbrQmfFrameState *state,
    SautiAacSbrLimiterReport *report,
    SautiAacSbrQmfStage *stage) {
    if(stage)*stage=SAUTI_SBR_QMF_STAGE_INPUT;
    if(!config || !config->hf || !config->hf->tables ||
       !config->hf->patches || !core_pcm ||
       (!export_qmf&&!output) || (export_qmf&&!qmf_output) ||
       !state || !report || !config->attack ||
       ((config->lpc==NULL)!=(config->chirp==NULL)) ||
       config->bs_smoothing_mode>1u ||
       state->ready>1u || state->frames==UINT64_MAX ||
       config->hf->slots<1u ||
       config->hf->slots>SAUTI_AAC_SBR_HF_MAX_SLOTS ||
       core_count<(size_t)config->hf->slots*32u ||
       (export_qmf?qmf_count:output_count)<
           (size_t)config->hf->slots*64u)
        return 0;
    if(stage)*stage=SAUTI_SBR_QMF_STAGE_MAP;
    if(!sauti_aac_sbr_hf_map_valid(
           config->hf->patches,config->hf->tables))
        return 0;
    if(stage)*stage=SAUTI_SBR_QMF_STAGE_HISTORY;
    /* State history is meaningful only after at least one
     * successful frame. Prevent an accidentally initialized 'warm'
     * state from smuggling garbage into the LPC predictor. */
    for(unsigned t=0;t<2u;t++)
        for(unsigned k=0;k<32u;k++) {
            const SautiAacSbrComplex c=state->low_prior[t][k];
            if(!isfinite(c.re) || !isfinite(c.im) ||
               (!state->ready && (c.re!=0.0f || c.im!=0.0f)))
                return 0;
        }
    if(!state->ready && state->frames!=0u)return 0;
    if(state->ready && (!state->frames ||
       state->analysis.samples_consumed<32u))return 0;

    const unsigned slots=config->hf->slots;
    const unsigned kx=config->hf->tables->kx;
    const unsigned k2=kx+config->hf->tables->m;
    /* A compact stack-only staging area; state is preserved across
     * EVERY failure, including errors in the final synthesis stage. */
    SautiAacSbrQmfFrameState next=*state;
    SautiAacSbrComplex low[SAUTI_AAC_SBR_HF_MAX_SLOTS*32u];
    SautiAacSbrComplex history[(SAUTI_AAC_SBR_HF_MAX_SLOTS+2u)*32u];
    SautiAacSbrComplex high[SAUTI_AAC_SBR_HF_MAX_SLOTS*64u];
    SautiAacSbrComplex assembled[SAUTI_AAC_SBR_HF_MAX_SLOTS*64u];
    SautiAacSbrComplex adjusted[SAUTI_AAC_SBR_HF_MAX_SLOTS*64u];
    float pcm[SAUTI_AAC_SBR_HF_MAX_SLOTS*64u];
    SautiAacSbrLimiterReport diagnostics;

    if(stage)*stage=SAUTI_SBR_QMF_STAGE_ANALYSIS;
    if(!sauti_aac_sbr_qmf_analyze_32(
           core_pcm,core_count,slots,32u,
           low,(size_t)slots*32u,&next.analysis))
        return 0;

    memcpy(history,next.low_prior,sizeof(next.low_prior));
    memcpy(history+2u*32u,low,(size_t)slots*32u*sizeof(low[0]));

    if(config->lpc) {
        if(stage)*stage=SAUTI_SBR_QMF_STAGE_PREDICT;
        if(!sauti_aac_sbr_hf_predict(
               config->hf->patches,config->hf->tables,
               history,((size_t)slots+2u)*32u,slots,
               config->lpc,config->chirp,high,
               (size_t)slots*64u))
            return 0;
    }else{
        if(stage)*stage=SAUTI_SBR_QMF_STAGE_TRANSPOSE;
        if(!sauti_aac_sbr_hf_transpose(
               config->hf->patches,config->hf->tables,
               low,(size_t)slots*32u,slots,
               high,(size_t)slots*64u))
            return 0;
    }

    /* For the current SBR-24 interface, high is an HF-only 64-bin
     * buffer. Mix low bins after HF adjustment so limiter energy
     * calculations do NOT accidentally treat old low bins as high. */
    if(stage)*stage=SAUTI_SBR_QMF_STAGE_HF_ADJUST;
    if(!sauti_aac_sbr_hf_joint_process_rom(
           config->hf,config->attack,config->bs_smoothing_mode,
           high,(size_t)slots*64u,
           adjusted,(size_t)slots*64u,&next.hf,&diagnostics))
        return 0;

    memset(assembled,0,(size_t)slots*64u*sizeof(assembled[0]));
    for(unsigned t=0;t<slots;t++) {
        for(unsigned k=0;k<kx;k++)
            assembled[(size_t)t*64u+k]=low[(size_t)t*32u+k];
        for(unsigned k=kx;k<k2;k++)
            assembled[(size_t)t*64u+k]=adjusted[(size_t)t*64u+k];
    }

    if(!export_qmf){
        if(stage)*stage=SAUTI_SBR_QMF_STAGE_SYNTHESIS;
        if(!sauti_aac_sbr_qmf_synth_64(
               assembled,(size_t)slots*64u,slots,
               pcm,(size_t)slots*64u,&next.synthesis))
            return 0;
    }

    /* The previous two low-frequency QMF slots are required by
     * SBR-13, even if this frame used the baseline transpose. */
    for(unsigned h=0;h<2u;h++)
        memcpy(next.low_prior[h],
               history+((size_t)slots+h)*32u,
               32u*sizeof(next.low_prior[h][0]));
    next.ready=1u;
    next.frames++;

    if(export_qmf)
        memcpy(qmf_output,assembled,(size_t)slots*64u*sizeof(*assembled));
    else
        memcpy(output,pcm,(size_t)slots*64u*sizeof(pcm[0]));
    *report=diagnostics;
    *state=next;
    if(stage)*stage=SAUTI_SBR_QMF_STAGE_OK;
    return 1;
}

int sauti_aac_sbr_qmf_frame_process_ex(
    const SautiAacSbrQmfFrameConfig *config,
    const float *core_pcm,size_t core_count,
    float *output,size_t output_count,
    SautiAacSbrQmfFrameState *state,
    SautiAacSbrLimiterReport *report,
    SautiAacSbrQmfStage *stage){
    return qmf_frame_process_impl(config,core_pcm,core_count,
               output,output_count,NULL,0u,0u,state,report,stage);
}

int sauti_aac_sbr_qmf_frame_export_qmf_ex(
    const SautiAacSbrQmfFrameConfig *config,
    const float *core_pcm,size_t core_count,
    SautiAacSbrComplex *qmf,size_t qmf_count,
    SautiAacSbrQmfFrameState *state,
    SautiAacSbrLimiterReport *report,
    SautiAacSbrQmfStage *stage){
    return qmf_frame_process_impl(config,core_pcm,core_count,
               NULL,0u,qmf,qmf_count,1u,state,report,stage);
}

int sauti_aac_sbr_qmf_frame_process(
    const SautiAacSbrQmfFrameConfig *config,
    const float *core_pcm,size_t core_count,
    float *output,size_t output_count,
    SautiAacSbrQmfFrameState *state,
    SautiAacSbrLimiterReport *report) {
    return sauti_aac_sbr_qmf_frame_process_ex(
        config,core_pcm,core_count,output,output_count,
        state,report,NULL);
}


/* PS-5c1: QMF analysis plus raw SBR-12 transpose, before HF adjustment.
 * All changes are staged, preserving the original PCM-producing path. */
int sauti_aac_sbr_qmf_frame_export_raw(
    const SautiAacSbrTables *tables,
    const SautiAacSbrPatches *patches,
    const float *core_pcm,size_t core_count,
    SautiAacSbrComplex *qmf,size_t qmf_count,
    SautiAacSbrQmfFrameState *state,
    SautiAacSbrQmfStage *stage){
    if(stage)*stage=SAUTI_SBR_QMF_STAGE_INPUT;
    if(!tables||!patches||!core_pcm||!qmf||!state||
       core_count<1024u||qmf_count<32u*64u||
       state->ready>1u||state->frames==UINT64_MAX||
       (!state->ready&&state->frames!=0u)||
       (state->ready&&(state->frames==0u||
                        state->analysis.samples_consumed<32u)))return 0;
    if(stage)*stage=SAUTI_SBR_QMF_STAGE_MAP;
    if(!sauti_aac_sbr_hf_map_valid(patches,tables)||
       tables->kx>32u||tables->kx+tables->m>64u)return 0;
    if(stage)*stage=SAUTI_SBR_QMF_STAGE_HISTORY;
    for(unsigned t=0;t<2u;t++)for(unsigned k=0;k<32u;k++){
        const SautiAacSbrComplex c=state->low_prior[t][k];
        if(!isfinite(c.re)||!isfinite(c.im)||
           (!state->ready&&(c.re!=0.0f||c.im!=0.0f)))return 0;
    }
    SautiAacSbrQmfFrameState next=*state;
    SautiAacSbrComplex low[32u*32u];
    SautiAacSbrComplex high[32u*64u];
    SautiAacSbrComplex combined[32u*64u];
    if(stage)*stage=SAUTI_SBR_QMF_STAGE_ANALYSIS;
    if(!sauti_aac_sbr_qmf_analyze_32(core_pcm,core_count,
                                     32u,32u,low,32u*32u,
                                     &next.analysis))return 0;
    if(stage)*stage=SAUTI_SBR_QMF_STAGE_TRANSPOSE;
    if(!sauti_aac_sbr_hf_transpose(patches,tables,low,32u*32u,
                                  32u,high,32u*64u))return 0;
    memcpy(combined,high,sizeof(combined));
    for(unsigned t=0;t<32u;t++)
        for(unsigned k=0;k<tables->kx;k++)
            combined[t*64u+k]=low[t*32u+k];
    memcpy(next.low_prior[0],low+30u*32u,sizeof(next.low_prior[0]));
    memcpy(next.low_prior[1],low+31u*32u,sizeof(next.low_prior[1]));
    next.ready=1u;
    next.frames++;
    memcpy(qmf,combined,sizeof(combined));
    *state=next;
    if(stage)*stage=SAUTI_SBR_QMF_STAGE_OK;
    return 1;
}
