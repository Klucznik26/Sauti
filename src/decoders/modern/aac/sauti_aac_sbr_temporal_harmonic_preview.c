/* SBR-54h: isolated experimental HF tone + raw-noise-gate QMF.
 * This does not modify gain, noise ROM, QMF input or PCM states. */
#include "sauti_aac_sbr_temporal_harmonic_preview.h"
#include <float.h>
#include <math.h>
#include <string.h>

#define SLOTS 32u
#define BANDS 64u
#define N (SLOTS*BANDS)

static int harmonic_preview_impl(
    const SautiAacSbrTemporalHfTargets *targets,
    const uint8_t onset_allowed[SLOTS],
    const SautiAacSbrTemporalNoiseFrame *noise,
    const SautiAacSbrTemporalNoiseReport *nr,
    const SautiAacSbrComplex *raw,size_t raw_count,
    SautiAacSbrComplex *output,size_t output_count,
    SautiAacSbrTemporalHarmonicPreviewState *state,
    SautiAacSbrTemporalHarmonicPreviewReport *report,
    unsigned tone_phase_offset){
    if(!targets || !onset_allowed || !noise || !nr || !raw ||
       !output || !state || !report || raw_count<N ||
       output_count<N || nr->emitted!=1u || nr->vectors>N ||
       (tone_phase_offset!=0u && tone_phase_offset!=2u) ||
       state->ready>1u || state->phase>3u ||
       (!state->ready && (state->frames || state->phase)) ||
       state->frames==UINT64_MAX ||
       nr->frame_index!=state->frames)return 0;
    SautiAacSbrComplex staging[N];
    SautiAacSbrTemporalHarmonicPreviewState next=*state;
    SautiAacSbrTemporalHarmonicPreviewReport stats={0};
    stats.frame_index=nr->frame_index;
    for(unsigned k=0u;k<BANDS;k++)
        if(state->previous[k]>1u ||
           (!state->ready && state->previous[k]))return 0;

    uint8_t running[BANDS];
    memcpy(running,state->previous,sizeof(running));
    for(unsigned t=0u;t<SLOTS;t++){
        const SautiAacSbrTemporalHfSlot *slot=&targets->slot[t];
        if(slot->source>1u || onset_allowed[t]>1u ||
           slot->kx<1u || slot->kx>32u ||
           slot->k2<=slot->kx || slot->k2>64u ||
           next.phase>3u)return 0;
        for(unsigned k=0u;k<BANDS;k++){
            const unsigned ix=t*BANDS+k;
            const unsigned on=k>=slot->kx && k<slot->k2;
            const unsigned flag=slot->harmonic[k];
            const SautiAacSbrComplex in=raw[ix],v=noise->vector[ix];
            const double amp=noise->raw_amplitude[ix];
            const double e=slot->energy[k],q=slot->noise_factor[k];
            if(noise->active[ix]!=(unsigned)on || flag>1u ||
               !isfinite(in.re)||!isfinite(in.im) ||
               !isfinite(v.re)||!isfinite(v.im) ||
               !isfinite(amp)||amp<0.0 ||
               !isfinite(e)||e<0.0 ||
               !isfinite(q)||q<0.0 ||
               (!on && (flag || amp!=0.0 || e!=0.0 || q!=0.0 ||
                        in.re!=0.0f || in.im!=0.0f)))
                return 0;
            staging[ix]=in;
            if(!on || !flag){
                running[k]=0u;
                continue;
            }
            stats.marked_bins++;
            if(!(running[k] || onset_allowed[t]))continue;
            const double tone_amp=sqrt(e/(1.0+q));
            const double subtract_re=(double)v.re*amp;
            const double subtract_im=(double)v.im*amp;
            if(!isfinite(tone_amp) ||
               !isfinite(subtract_re)||!isfinite(subtract_im))
                return 0;
            /* SBR-69 diagnostic: +2 rotates ONLY the added tone by pi.
             * The noise gate, per-slot envelope ownership and state clock
             * remain byte-for-byte the same as in the original preview. */
            const unsigned tone_phase=(next.phase+tone_phase_offset)&3u;
            double sign=1.0;
            if(tone_phase==2u ||
               (tone_phase==1u && (k&1u)) ||
               (tone_phase==3u && !(k&1u)))
                sign=-1.0;
            const double re=(double)in.re-subtract_re+
                ((tone_phase&1u)?0.0:sign*tone_amp);
            const double im=(double)in.im-subtract_im+
                ((tone_phase&1u)?sign*tone_amp:0.0);
            if(!isfinite(re)||!isfinite(im) ||
               fabs(re)>FLT_MAX || fabs(im)>FLT_MAX)
                return 0;
            staging[ix].re=(float)re;
            staging[ix].im=(float)im;
            stats.active_tones++;
            stats.gated_noise_bins++;
            stats.tonal_power+=tone_amp*tone_amp;
            stats.removed_noise_power+=subtract_re*subtract_re+
                                       subtract_im*subtract_im;
            running[k]=1u;
        }
        next.phase=(uint8_t)((next.phase+1u)&3u);
    }
    if(!isfinite(stats.tonal_power) ||
       !isfinite(stats.removed_noise_power) ||
       stats.active_tones>stats.marked_bins ||
       stats.gated_noise_bins>nr->vectors)
        return 0;
    memcpy(next.previous,running,sizeof(running));
    next.ready=1u;
    next.frames++;
    memcpy(output,staging,sizeof(staging));
    *state=next;
    *report=stats;
    return 1;
}

/* Original experimental SBR-54h behavior and ABI remain unchanged. */
int sauti_aac_sbr_temporal_harmonic_preview(
    const SautiAacSbrTemporalHfTargets *targets,
    const uint8_t onset_allowed[SLOTS],
    const SautiAacSbrTemporalNoiseFrame *noise,
    const SautiAacSbrTemporalNoiseReport *nr,
    const SautiAacSbrComplex *raw,size_t raw_count,
    SautiAacSbrComplex *output,size_t output_count,
    SautiAacSbrTemporalHarmonicPreviewState *state,
    SautiAacSbrTemporalHarmonicPreviewReport *report){
    return harmonic_preview_impl(targets,onset_allowed,noise,nr,
        raw,raw_count,output,output_count,state,report,0u);
}

/* SBR-73: scale only the sinusoid that the unscaled preview added.
 * 1024/sqrt(2) is 32768/(32*sqrt(2)): post-boost s_m divided into
 * the gain-adjusted QMF domain. No constant boost is baked in. */
int sauti_aac_sbr_temporal_harmonic_scale(
    const SautiAacSbrComplex *gain,size_t gain_count,
    const SautiAacSbrComplex *raw,size_t raw_count,
    const SautiAacSbrComplex *sine,size_t sine_count,
    const double boost[N],
    const uint8_t apply[N],
    SautiAacSbrComplex *output,size_t output_count){
    if(!gain || !raw || !sine || !boost || !apply || !output ||
       gain_count<N || raw_count<N || sine_count<N || output_count<N)
        return 0;
    SautiAacSbrComplex staging[N];
    const double domain=1024.0/sqrt(2.0);
    for(unsigned ix=0u;ix<N;ix++){
        const unsigned use=apply[ix];
        const SautiAacSbrComplex g=gain[ix],r=raw[ix],s=sine[ix];
        if(use>1u ||
           !isfinite(g.re) || !isfinite(g.im) ||
           !isfinite(r.re) || !isfinite(r.im) ||
           !isfinite(s.re) || !isfinite(s.im))
            return 0;
        if(!use || (s.re==r.re && s.im==r.im)){
            staging[ix]=s;
            continue;
        }
        const double b=boost[ix];
        if(!isfinite(b) || b<0.0)return 0;
        const double factor=b*domain;
        const double re=(double)g.re+((double)s.re-(double)g.re)*factor;
        const double im=(double)g.im+((double)s.im-(double)g.im)*factor;
        if(!isfinite(re) || !isfinite(im) ||
           fabs(re)>FLT_MAX || fabs(im)>FLT_MAX)
            return 0;
        staging[ix].re=(float)re;
        staging[ix].im=(float)im;
    }
    memcpy(output,staging,sizeof(staging));
    return 1;
}

/* SBR-69 hypothesis only: +2 four-slot oscillator phases, same ROM noise. */
int sauti_aac_sbr_temporal_harmonic_preview_phase2(
    const SautiAacSbrTemporalHfTargets *targets,
    const uint8_t onset_allowed[SLOTS],
    const SautiAacSbrTemporalNoiseFrame *noise,
    const SautiAacSbrTemporalNoiseReport *nr,
    const SautiAacSbrComplex *raw,size_t raw_count,
    SautiAacSbrComplex *output,size_t output_count,
    SautiAacSbrTemporalHarmonicPreviewState *state,
    SautiAacSbrTemporalHarmonicPreviewReport *report){
    return harmonic_preview_impl(targets,onset_allowed,noise,nr,
        raw,raw_count,output,output_count,state,report,2u);
}
