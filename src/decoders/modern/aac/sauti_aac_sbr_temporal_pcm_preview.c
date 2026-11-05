/* SBR-54a: temporally aligned LOW QMF + delayed HIGH QMF preview
 * through the existing 64-band synthesis bank. Not normative HE-AAC. */
#include "sauti_aac_sbr_temporal_pcm_preview.h"
#include <math.h>
#include <string.h>

#define PCM_SLOTS 32u
#define PCM_BANDS 64u
#define PCM_LOW 32u
#define PCM_OUT (PCM_SLOTS*PCM_BANDS)

static int preview_state_valid(
    const SautiAacSbrTemporalPcmPreviewState *s){
    if(!s || s->ready>1u || s->closed>1u ||
       (!s->ready && (s->frames || s->emitted_frames || s->closed)) ||
       (s->ready && !s->frames) ||
       s->emitted_frames>s->frames ||
       (s->ready && !s->closed &&
        s->emitted_frames+1u!=s->frames) ||
       (s->closed && s->emitted_frames!=s->frames))
        return 0;
    for(unsigned i=0u;i<PCM_SLOTS*PCM_LOW;i++){
        const SautiAacSbrComplex x=s->held_low[i];
        if(!isfinite(x.re) || !isfinite(x.im))return 0;
        if(!s->ready && (x.re!=0.0f || x.im!=0.0f))
            return 0;
    }
    return 1;
}

static int render(const SautiAacSbrComplex *low,
                  const SautiAacSbrComplex *high,size_t high_count,
                  const SautiAacSbrTemporalNoiseFrame *noise,
                  const SautiAacSbrTemporalApplyReport *apply,
                  const SautiAacSbrTemporalMixReport *mix,
                  SautiAacSbrQmfSynthWindowState *synth,
                  float pcm[PCM_OUT],
                  SautiAacSbrTemporalPcmPreviewReport *stats){
    if(!low || !high || !noise || !apply || !mix || !synth ||
       high_count<PCM_OUT || apply->emitted!=1u ||
       mix->frame_index!=apply->frame_index ||
       mix->active_bins>PCM_OUT ||
       mix->noise_bins>mix->active_bins ||
       !isfinite(mix->noise_rms) || mix->noise_rms<0.0)
        return 0;

    SautiAacSbrComplex assembled[PCM_OUT]={0};
    unsigned counted=0u;
    for(unsigned t=0u;t<PCM_SLOTS;t++){
        unsigned kx=PCM_BANDS,k2=0u;
        unsigned first=0u,last=0u;
        for(unsigned k=0u;k<PCM_BANDS;k++){
            const unsigned i=t*PCM_BANDS+k;
            const unsigned active=noise->active[i];
            const SautiAacSbrComplex x=high[i];
            if(active>1u || !isfinite(x.re) || !isfinite(x.im))
                return 0;
            if(active){
                if(!first)kx=k;
                if(last && k!=k2)return 0;
                k2=k+1u;
                first=1u;
                last=1u;
                counted++;
            }else{
                if(first)last=1u;
                if(x.re!=0.0f || x.im!=0.0f)return 0;
                if(first)last=2u; /* no gaps after HF begins */
            }
            if(first && last==2u && active)return 0;
        }
        if(!first || kx<1u || kx>PCM_LOW ||
           k2<=kx || k2>PCM_BANDS)return 0;
        for(unsigned k=0u;k<PCM_LOW;k++){
            const SautiAacSbrComplex x=low[t*PCM_LOW+k];
            if(!isfinite(x.re) || !isfinite(x.im))return 0;
            if(k<kx)assembled[t*PCM_BANDS+k]=x;
        }
        for(unsigned k=kx;k<k2;k++)
            assembled[t*PCM_BANDS+k]=high[t*PCM_BANDS+k];
    }
    if(counted!=mix->active_bins)return 0;
    if(!sauti_aac_sbr_qmf_synth_64(
           assembled,PCM_OUT,PCM_SLOTS,pcm,PCM_OUT,synth))
        return 0;
    double power=0.0,peak=0.0;
    unsigned nonzero=0u;
    for(unsigned i=0u;i<PCM_OUT;i++){
        const double x=pcm[i];
        if(!isfinite(x))return 0;
        power+=x*x;
        if(fabs(x)>peak)peak=fabs(x);
        if(x!=0.0)nonzero++;
    }
    stats->emitted=1u;
    stats->frame_index=apply->frame_index;
    stats->samples=PCM_OUT;
    stats->nonzero=nonzero;
    stats->rms=sqrt(power/(double)PCM_OUT);
    stats->peak=peak;
    return isfinite(stats->rms) && isfinite(stats->peak);
}

int sauti_aac_sbr_temporal_pcm_preview_push(
    const SautiAacSbrComplex *current_low,size_t low_count,
    const SautiAacSbrComplex *emitted_high,size_t high_count,
    const SautiAacSbrTemporalNoiseFrame *noise,
    const SautiAacSbrTemporalApplyReport *apply_report,
    const SautiAacSbrTemporalMixReport *mix_report,
    SautiAacSbrTemporalPcmPreviewState *state,
    float *pcm,size_t pcm_count,
    SautiAacSbrTemporalPcmPreviewReport *report){
    if(!current_low || !apply_report || !state || !pcm || !report ||
       low_count<PCM_SLOTS*PCM_LOW || pcm_count<PCM_OUT ||
       !preview_state_valid(state) || state->closed ||
       state->frames==UINT64_MAX ||
       apply_report->emitted!=(unsigned)state->ready)
        return 0;
    for(unsigned i=0u;i<PCM_SLOTS*PCM_LOW;i++)
        if(!isfinite(current_low[i].re) ||
           !isfinite(current_low[i].im))return 0;
    SautiAacSbrTemporalPcmPreviewState next=*state;
    SautiAacSbrTemporalPcmPreviewReport stats={0};
    float rendered[PCM_OUT];
    if(state->ready){
        if(apply_report->frame_index!=state->frames-1u ||
           !render(state->held_low,emitted_high,high_count,
                   noise,apply_report,mix_report,&next.synth,
                   rendered,&stats))
            return 0;
        next.emitted_frames++;
    }
    memcpy(next.held_low,current_low,sizeof(next.held_low));
    next.frames++;
    next.ready=1u;
    if(stats.emitted)memcpy(pcm,rendered,sizeof(rendered));
    *report=stats;
    *state=next;
    return 1;
}

int sauti_aac_sbr_temporal_pcm_preview_flush(
    const SautiAacSbrComplex *emitted_high,size_t high_count,
    const SautiAacSbrTemporalNoiseFrame *noise,
    const SautiAacSbrTemporalApplyReport *apply_report,
    const SautiAacSbrTemporalMixReport *mix_report,
    SautiAacSbrTemporalPcmPreviewState *state,
    float *pcm,size_t pcm_count,
    SautiAacSbrTemporalPcmPreviewReport *report){
    if(!state || !pcm || !report ||
       pcm_count<PCM_OUT || !preview_state_valid(state) ||
       !state->ready || state->closed ||
       !apply_report ||
       apply_report->frame_index!=state->frames-1u)
        return 0;
    SautiAacSbrTemporalPcmPreviewState next=*state;
    SautiAacSbrTemporalPcmPreviewReport stats={0};
    float rendered[PCM_OUT];
    if(!render(state->held_low,emitted_high,high_count,
               noise,apply_report,mix_report,&next.synth,
               rendered,&stats))return 0;
    next.emitted_frames++;
    next.closed=1u;
    memcpy(pcm,rendered,sizeof(rendered));
    *report=stats;
    *state=next;
    return 1;
}
