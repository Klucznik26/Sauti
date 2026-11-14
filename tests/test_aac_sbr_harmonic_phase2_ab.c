/* SBR-69: QMF-domain A/B must flip ONLY tonal oscillator's phase.
 * No change to raw-noise gate, onset, owner, state clock or rollback.
 * A PASS does not establish which phase is normative for HE-AAC. */
#include "sauti_aac_sbr_temporal_harmonic_preview.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define N (32u*64u)
#define AMP 2.8284271247461900976

static void init(
    SautiAacSbrTemporalHfTargets *targets,
    SautiAacSbrTemporalNoiseFrame *noise,
    SautiAacSbrComplex raw[N],
    uint8_t onset[32]){
    memset(targets,0,sizeof(*targets));
    memset(noise,0,sizeof(*noise));
    memset(raw,0,N*sizeof(raw[0]));
    for(unsigned t=0u;t<32u;t++){
        SautiAacSbrTemporalHfSlot *s=&targets->slot[t];
        s->kx=16u;s->k2=19u;
        for(unsigned k=16u;k<19u;k++){
            const unsigned i=t*64u+k;
            s->energy[k]=16.0;
            s->noise_factor[k]=1.0;
            noise->active[i]=1u;
            noise->vector[i].re=0.5f;
            noise->vector[i].im=-0.25f;
            noise->raw_amplitude[i]=AMP;
            raw[i].re=(float)(2.0+0.5*AMP);
            raw[i].im=(float)(1.0-0.25*AMP);
        }
        s->harmonic[17u]=1u; /* odd parity */
        s->harmonic[18u]=1u; /* even parity */
        onset[t]=(uint8_t)(t>=3u);
    }
}
static void near(double actual,double expected){
    assert(isfinite(actual));
    assert(fabs(actual-expected)<3e-5);
}
static void unchanged(
    const SautiAacSbrComplex a[N],
    const SautiAacSbrComplex b[N],size_t index){
    assert(memcmp(&a[index],&b[index],sizeof(a[0]))==0);
}
int main(void){
    SautiAacSbrTemporalHfTargets targets;
    SautiAacSbrTemporalNoiseFrame noise;
    SautiAacSbrComplex raw[N],normal[N],phase2[N],prior_normal[N],prior_phase2[N];
    uint8_t onset[32];
    init(&targets,&noise,raw,onset);
    SautiAacSbrTemporalNoiseReport nr={0};
    nr.emitted=1u;nr.vectors=96u;nr.frame_index=0u;
    SautiAacSbrTemporalHarmonicPreviewState original={0},alternate={0};
    SautiAacSbrTemporalHarmonicPreviewReport report0={0},report2={0};

    assert(sauti_aac_sbr_temporal_harmonic_preview(
        &targets,onset,&noise,&nr,raw,N,normal,N,&original,&report0));
    assert(sauti_aac_sbr_temporal_harmonic_preview_phase2(
        &targets,onset,&noise,&nr,raw,N,phase2,N,&alternate,&report2));
    assert(memcmp(&original,&alternate,sizeof(original))==0);
    assert(original.frames==1u && original.phase==0u &&
           original.previous[17u]==1u && original.previous[18u]==1u);
    assert(report0.marked_bins==64u && report0.active_tones==58u &&
           report2.active_tones==58u);
    near(report0.tonal_power,report2.tonal_power);
    near(report0.removed_noise_power,report2.removed_noise_power);
    for(unsigned t=0u;t<32u;t++)
        for(unsigned k=0u;k<64u;k++){
            const unsigned i=t*64u+k;
            if(t<3u || (k!=17u && k!=18u)){
                unchanged(raw,normal,i);
                unchanged(raw,phase2,i);
                continue;
            }
            const double gated_re=(double)raw[i].re-
                (double)noise.vector[i].re*noise.raw_amplitude[i];
            const double gated_im=(double)raw[i].im-
                (double)noise.vector[i].im*noise.raw_amplitude[i];
            /* Same noise gate and magnitude, opposite *tone* only. */
            near((double)normal[i].re+(double)phase2[i].re,2.0*gated_re);
            near((double)normal[i].im+(double)phase2[i].im,2.0*gated_im);
            const double tone0re=(double)normal[i].re-gated_re;
            const double tone0im=(double)normal[i].im-gated_im;
            const double tone2re=(double)phase2[i].re-gated_re;
            const double tone2im=(double)phase2[i].im-gated_im;
            near(tone0re,-tone2re);
            near(tone0im,-tone2im);
            near(hypot(tone0re,tone0im),sqrt(8.0));
        }
    puts("SBR-69: phase+2 reverses ONLY harmonic QMF tone, preserves raw-noise gate PASS");

    /* Cross-frame tonal continuation must remain identical. */
    memset(onset,0,sizeof(onset));
    nr.frame_index=1u;
    assert(sauti_aac_sbr_temporal_harmonic_preview(
        &targets,onset,&noise,&nr,raw,N,normal,N,&original,&report0));
    assert(sauti_aac_sbr_temporal_harmonic_preview_phase2(
        &targets,onset,&noise,&nr,raw,N,phase2,N,&alternate,&report2));
    assert(memcmp(&original,&alternate,sizeof(original))==0);
    assert(report0.active_tones==64u && report2.active_tones==64u);
    assert(original.frames==2u && original.phase==0u);

    /* Dropping tonal flags must yield byte-identical raw input on both paths. */
    for(unsigned t=0u;t<32u;t++){
        targets.slot[t].harmonic[17u]=0u;
        targets.slot[t].harmonic[18u]=0u;
    }
    nr.frame_index=2u;
    assert(sauti_aac_sbr_temporal_harmonic_preview(
        &targets,onset,&noise,&nr,raw,N,normal,N,&original,&report0));
    assert(sauti_aac_sbr_temporal_harmonic_preview_phase2(
        &targets,onset,&noise,&nr,raw,N,phase2,N,&alternate,&report2));
    assert(memcmp(&original,&alternate,sizeof(original))==0);
    assert(memcmp(normal,raw,sizeof(raw))==0);
    assert(memcmp(phase2,raw,sizeof(raw))==0);
    assert(original.previous[17u]==0u && original.previous[18u]==0u);
    puts("SBR-69: FIXVAR-style continuation, phase persistence and tonal bypass PASS");

    /* Failure must not mutate alternate output, report, or state. */
    memcpy(prior_normal,normal,sizeof(normal));
    memcpy(prior_phase2,phase2,sizeof(phase2));
    SautiAacSbrTemporalHarmonicPreviewState saved=alternate;
    SautiAacSbrTemporalHarmonicPreviewReport saved_report=report2;
    nr.frame_index=3u;
    raw[31u*64u+17u].re=NAN;
    assert(!sauti_aac_sbr_temporal_harmonic_preview_phase2(
        &targets,onset,&noise,&nr,raw,N,phase2,N,&alternate,&report2));
    assert(memcmp(&alternate,&saved,sizeof(saved))==0);
    assert(memcmp(&report2,&saved_report,sizeof(saved_report))==0);
    assert(memcmp(phase2,prior_phase2,sizeof(phase2))==0);
    assert(memcmp(normal,prior_normal,sizeof(normal))==0);
    raw[31u*64u+17u].re=(float)(2.0+0.5*AMP);
    nr.frame_index=4u; /* invalid continuity */
    assert(!sauti_aac_sbr_temporal_harmonic_preview_phase2(
        &targets,onset,&noise,&nr,raw,N,phase2,N,&alternate,&report2));
    assert(memcmp(&alternate,&saved,sizeof(saved))==0);
    assert(memcmp(phase2,prior_phase2,sizeof(phase2))==0);
    puts("PASS SBR-69 experimental QMF harmonic phase+2, no production PCM changes");
    return 0;
}
