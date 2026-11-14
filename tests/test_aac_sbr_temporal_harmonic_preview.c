#include "sauti_aac_sbr_temporal_harmonic_preview.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define N (32u*64u)

static void input(SautiAacSbrTemporalHfTargets *targets,
                  SautiAacSbrTemporalNoiseFrame *noise,
                  SautiAacSbrComplex raw[N],
                  uint8_t allow[32]){
    memset(targets,0,sizeof(*targets));
    memset(noise,0,sizeof(*noise));
    memset(raw,0,N*sizeof(*raw));
    for(unsigned t=0u;t<32u;t++){
        SautiAacSbrTemporalHfSlot *s=&targets->slot[t];
        s->kx=16u;s->k2=18u;
        s->energy[16u]=s->energy[17u]=16.0;
        s->noise_factor[16u]=s->noise_factor[17u]=1.0;
        s->harmonic[17u]=1u; /* odd bin, 4-phase sign parity */
        allow[t]=(uint8_t)(t>=3u);
        for(unsigned k=16u;k<18u;k++){
            const unsigned ix=t*64u+k;
            noise->active[ix]=1u;
            noise->vector[ix]=(SautiAacSbrComplex){0.5f,-0.25f};
            noise->raw_amplitude[ix]=sqrt(8.0);
            raw[ix].re=(float)(2.0+0.5*sqrt(8.0));
            raw[ix].im=(float)(1.0-0.25*sqrt(8.0));
        }
    }
}
static void close_to(double x,double want){
    assert(isfinite(x) && fabs(x-want)<2e-5);
}
int main(void){
    SautiAacSbrTemporalHfTargets targets;
    SautiAacSbrTemporalNoiseFrame noise;
    SautiAacSbrComplex raw[N],out[N];
    uint8_t allow[32];
    input(&targets,&noise,raw,allow);
    SautiAacSbrTemporalNoiseReport nr={0};
    nr.emitted=1u;nr.vectors=64u;nr.frame_index=0u;
    SautiAacSbrTemporalHarmonicPreviewState st={0};
    SautiAacSbrTemporalHarmonicPreviewReport rep={0};
    assert(sauti_aac_sbr_temporal_harmonic_preview(
        &targets,allow,&noise,&nr,raw,N,out,N,&st,&rep));
    assert(rep.frame_index==0u && rep.marked_bins==32u &&
           rep.active_tones==29u && rep.gated_noise_bins==29u);
    assert(st.ready==1u && st.frames==1u && st.phase==0u &&
           st.previous[17u]==1u);
    assert(memcmp(out,raw,3u*64u*sizeof(raw[0]))==0);
    const double tone=sqrt(8.0);
    close_to(out[3u*64u+17u].re,2.0);
    close_to(out[3u*64u+17u].im,1.0+tone);
    close_to(out[4u*64u+17u].re,2.0+tone);
    close_to(out[4u*64u+17u].im,1.0);
    assert(memcmp(&out[0u*64u+16u],&raw[16u],sizeof(raw[0]))==0);
    puts("SBR-54h: transient 3-slot onset, 4-phase/parity, gated tonal ROM PASS");

    /* The prior harmonic must continue even with no new onset. */
    memset(allow,0,sizeof(allow));
    nr.frame_index=1u;
    assert(sauti_aac_sbr_temporal_harmonic_preview(
        &targets,allow,&noise,&nr,raw,N,out,N,&st,&rep));
    assert(rep.active_tones==32u && st.frames==2u);
    close_to(out[17u].re,2.0+tone);

    /* Valid no-harmonic frame removes any previous tonal presence. */
    for(unsigned t=0u;t<32u;t++)
        targets.slot[t].harmonic[17u]=0u;
    nr.frame_index=2u;
    assert(sauti_aac_sbr_temporal_harmonic_preview(
        &targets,allow,&noise,&nr,raw,N,out,N,&st,&rep));
    assert(rep.active_tones==0u && st.previous[17u]==0u);
    assert(memcmp(out,raw,sizeof(raw))==0);
    puts("SBR-54h: continued tone, flag removal and exact bypass PASS");

    SautiAacSbrTemporalHarmonicPreviewState old=st;
    SautiAacSbrComplex saved[N];
    memcpy(saved,out,sizeof(saved));
    nr.frame_index=3u;
    raw[31u*64u+17u].re=NAN;
    assert(!sauti_aac_sbr_temporal_harmonic_preview(
        &targets,allow,&noise,&nr,raw,N,out,N,&st,&rep));
    assert(memcmp(&old,&st,sizeof(st))==0 &&
           memcmp(saved,out,sizeof(saved))==0);
    raw[31u*64u+17u].re=(float)(2.0+0.5*sqrt(8.0));
    nr.frame_index=4u;
    assert(!sauti_aac_sbr_temporal_harmonic_preview(
        &targets,allow,&noise,&nr,raw,N,out,N,&st,&rep));
    assert(memcmp(&old,&st,sizeof(st))==0 &&
           memcmp(saved,out,sizeof(saved))==0);
    puts("PASS SBR-54h delayed temporal harmonic/noise-gate (no normative HE-AAC)");

    /* The unscaled oscillator above stays at sqrt(E/(1+Q)). This
     * sibling only multiplies an already-added tone by the caller
     * boost times 1024/sqrt(2). */
    {
        SautiAacSbrComplex gain[N],raw[N],sine[N],scaled[N];
        double tone_boost[N];
        uint8_t apply[N];
        memset(gain,0,sizeof(gain));
        memset(raw,0,sizeof(raw));
        memset(sine,0,sizeof(sine));
        memset(tone_boost,0,sizeof(tone_boost));
        memset(apply,0,sizeof(apply));
        const unsigned tonal=17u,held=18u,untouched=19u;
        gain[tonal]=(SautiAacSbrComplex){1.f,2.f};
        raw[tonal]=(SautiAacSbrComplex){1.5f,2.25f};
        sine[tonal]=(SautiAacSbrComplex){1.5f,2.75f};
        tone_boost[tonal]=1.25;
        apply[tonal]=1u;
        gain[held]=raw[held]=sine[held]=(SautiAacSbrComplex){3.f,-4.f};
        tone_boost[held]=9.0;
        apply[held]=1u;
        gain[untouched]=(SautiAacSbrComplex){5.f,6.f};
        raw[untouched]=(SautiAacSbrComplex){5.f,6.f};
        sine[untouched]=(SautiAacSbrComplex){7.f,8.f};
        tone_boost[untouched]=4.0;
        scaled[0]=(SautiAacSbrComplex){42.f,-42.f};
        assert(sauti_aac_sbr_temporal_harmonic_scale(
            gain,N,raw,N,sine,N,tone_boost,apply,scaled,N));
        const double factor=1.25*(1024.0/sqrt(2.0));
        close_to(scaled[tonal].re,1.0+(1.5-1.0)*factor);
        close_to(scaled[tonal].im,2.0+(2.75-2.0)*factor);
        close_to(scaled[held].re,3.0);
        close_to(scaled[held].im,-4.0);
        close_to(scaled[untouched].re,7.0);
        close_to(scaled[untouched].im,8.0);
        tone_boost[tonal]=NAN;
        scaled[0]=(SautiAacSbrComplex){42.f,-42.f};
        assert(!sauti_aac_sbr_temporal_harmonic_scale(
            gain,N,raw,N,sine,N,tone_boost,apply,scaled,N));
        close_to(scaled[0].re,42.0);
        close_to(scaled[0].im,-42.0);
        puts("SBR-73: per-band tone scale leaves the unscaled oscillator PASS");
    }
    return 0;
}
