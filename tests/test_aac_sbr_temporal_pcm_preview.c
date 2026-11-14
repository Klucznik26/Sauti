/* SBR-54a: real 2048 float waveform samples from temporally
 * matched low and delayed high QMF; not HE-AAC conformance PCM. */
#include "sauti_aac_sbr_temporal_pcm_preview.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define SLOTS 32u
#define LOW 32u
#define BANDS 64u
#define COUNT (SLOTS*BANDS)

static void make_low(SautiAacSbrComplex low[SLOTS*LOW],
                     unsigned frame){
    for(unsigned t=0u;t<SLOTS;t++)
        for(unsigned k=0u;k<LOW;k++){
            SautiAacSbrComplex *x=&low[t*LOW+k];
            x->re=(float)(0.01*(double)(frame+1u)+
                          0.00001*(double)(t*LOW+k));
            x->im=(float)(0.002*(double)(frame+1u));
        }
}
static void make_high(SautiAacSbrComplex high[COUNT],
                      SautiAacSbrTemporalNoiseFrame *noise,
                      unsigned frame,unsigned tail){
    memset(high,0,COUNT*sizeof(*high));
    memset(noise,0,sizeof(*noise));
    for(unsigned t=0u;t<SLOTS;t++){
        const unsigned inherited=frame==1u && t<tail;
        const unsigned kx=(frame==0u || inherited)?20u:16u;
        const unsigned k2=kx+8u;
        for(unsigned k=kx;k<k2;k++){
            const unsigned i=t*BANDS+k;
            noise->active[i]=1u;
            high[i].re=(float)(0.03*(double)(frame+1u)+
                               0.001*(double)t+
                               0.0001*(double)k);
            high[i].im=(float)(0.002*(double)(k-kx));
        }
    }
}
static void manual(const SautiAacSbrComplex low[SLOTS*LOW],
                   const SautiAacSbrComplex high[COUNT],
                   const SautiAacSbrTemporalNoiseFrame *noise,
                   SautiAacSbrQmfSynthWindowState *synth,
                   float pcm[COUNT]){
    SautiAacSbrComplex assembled[COUNT]={0};
    for(unsigned t=0u;t<SLOTS;t++){
        unsigned kx=0u;
        for(unsigned k=0u;k<BANDS;k++)
            if(noise->active[t*BANDS+k]){kx=k;break;}
        assert(kx>0u && kx<=LOW);
        for(unsigned k=0u;k<kx;k++)
            assembled[t*BANDS+k]=low[t*LOW+k];
        for(unsigned k=kx;k<BANDS;k++)
            assembled[t*BANDS+k]=high[t*BANDS+k];
    }
    assert(sauti_aac_sbr_qmf_synth_64(
        assembled,COUNT,SLOTS,pcm,COUNT,synth));
}
static void verify_failure(
    const SautiAacSbrComplex low[SLOTS*LOW],
    const SautiAacSbrComplex high[COUNT],
    const SautiAacSbrTemporalNoiseFrame *noise,
    const SautiAacSbrTemporalApplyReport *apply,
    const SautiAacSbrTemporalMixReport *mix,
    SautiAacSbrTemporalPcmPreviewState *state,
    float pcm[COUNT],SautiAacSbrTemporalPcmPreviewReport *stats){
    SautiAacSbrTemporalPcmPreviewState old_state=*state;
    float old_pcm[COUNT];
    memcpy(old_pcm,pcm,sizeof(old_pcm));
    const SautiAacSbrTemporalPcmPreviewReport old_stats=*stats;
    assert(!sauti_aac_sbr_temporal_pcm_preview_push(
        low,SLOTS*LOW,high,COUNT,noise,apply,mix,
        state,pcm,COUNT,stats));
    assert(memcmp(&old_state,state,sizeof(old_state))==0);
    assert(memcmp(old_pcm,pcm,sizeof(old_pcm))==0);
    assert(memcmp(&old_stats,stats,sizeof(old_stats))==0);
}

static void run(unsigned tail){
    SautiAacSbrTemporalPcmPreviewState state={0};
    SautiAacSbrQmfSynthWindowState reference={0};
    SautiAacSbrTemporalPcmPreviewReport stats={0};
    SautiAacSbrTemporalNoiseFrame noise;
    SautiAacSbrTemporalApplyReport apply={0};
    SautiAacSbrTemporalMixReport mix={0};
    SautiAacSbrComplex low[SLOTS*LOW],held_low[SLOTS*LOW];
    SautiAacSbrComplex high[COUNT];
    float out[COUNT],expected[COUNT];
    memset(out,0x55,sizeof(out));
    float untouched[COUNT];
    memcpy(untouched,out,sizeof(out));

    make_low(low,0u);memcpy(held_low,low,sizeof(held_low));
    assert(sauti_aac_sbr_temporal_pcm_preview_push(
        low,SLOTS*LOW,NULL,0u,NULL,&apply,NULL,
        &state,out,COUNT,&stats));
    assert(!stats.emitted && state.frames==1u &&
           state.emitted_frames==0u &&
           memcmp(out,untouched,sizeof(out))==0);
    assert(!sauti_aac_sbr_temporal_pcm_preview_flush(
        NULL,0u,NULL,&apply,NULL,&state,out,COUNT,&stats));
    assert(memcmp(out,untouched,sizeof(out))==0);

    for(unsigned frame=1u;frame<3u;frame++){
        make_low(low,frame);
        make_high(high,&noise,frame-1u,tail);
        apply.emitted=1u;
        apply.frame_index=frame-1u;
        mix.frame_index=frame-1u;
        mix.active_bins=SLOTS*8u;
        mix.noise_bins=SLOTS*8u;
        mix.noise_rms=1.0;
        SautiAacSbrTemporalMixReport invalid_mix=mix;
        invalid_mix.frame_index++;
        verify_failure(low,high,&noise,&apply,&invalid_mix,
                       &state,out,&stats);
        SautiAacSbrTemporalNoiseFrame bad_noise=noise;
        bad_noise.active[31u*BANDS+63u]=1u;
        verify_failure(low,high,&bad_noise,&apply,&mix,
                       &state,out,&stats);
        SautiAacSbrComplex bad_high[COUNT];
        memcpy(bad_high,high,sizeof(bad_high));
        bad_high[31u*BANDS+63u].re=NAN;
        verify_failure(low,bad_high,&noise,&apply,&mix,
                       &state,out,&stats);
        SautiAacSbrComplex bad_low[SLOTS*LOW];
        memcpy(bad_low,low,sizeof(bad_low));
        bad_low[31u*LOW+31u].im=NAN;
        verify_failure(bad_low,high,&noise,&apply,&mix,
                       &state,out,&stats);

        manual(held_low,high,&noise,&reference,expected);
        assert(sauti_aac_sbr_temporal_pcm_preview_push(
            low,SLOTS*LOW,high,COUNT,&noise,&apply,&mix,
            &state,out,COUNT,&stats));
        assert(stats.emitted && stats.frame_index==frame-1u &&
               stats.samples==COUNT && stats.nonzero>0u &&
               stats.rms>0.0 && stats.peak>0.0 &&
               state.frames==frame+1u &&
               state.emitted_frames==frame);
        assert(memcmp(out,expected,sizeof(out))==0);
        memcpy(held_low,low,sizeof(held_low));
    }
    /* The final high QMF belongs to frame 2, whose low QMF
     * was intentionally retained during processing frame 2. */
    make_high(high,&noise,2u,tail);
    apply.frame_index=2u;mix.frame_index=2u;
    manual(held_low,high,&noise,&reference,expected);
    assert(sauti_aac_sbr_temporal_pcm_preview_flush(
        high,COUNT,&noise,&apply,&mix,&state,out,COUNT,&stats));
    assert(stats.emitted && stats.frame_index==2u &&
           stats.samples==COUNT && stats.nonzero>0u &&
           state.closed && state.emitted_frames==3u);
    assert(memcmp(out,expected,sizeof(out))==0);
    SautiAacSbrTemporalPcmPreviewState old_state=state;
    SautiAacSbrTemporalPcmPreviewReport old_stats=stats;
    float old_output[COUNT];memcpy(old_output,out,sizeof(out));
    assert(!sauti_aac_sbr_temporal_pcm_preview_flush(
        high,COUNT,&noise,&apply,&mix,&state,out,COUNT,&stats));
    assert(memcmp(&old_state,&state,sizeof(state))==0);
    assert(memcmp(&old_stats,&stats,sizeof(stats))==0);
    assert(memcmp(old_output,out,sizeof(out))==0);
    printf("SBR-54a: %u-slot geometry, 3 x 2048 PCM preview samples, continuous synthesis and rollback PASS\n",tail);
}
int main(void){
    run(2u);
    run(6u);
    puts("PASS SBR-54a delayed 64-band QMF PCM PREVIEW (not normative HE-AAC)");
    return 0;
}
