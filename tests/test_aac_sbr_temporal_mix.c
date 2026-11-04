/* SBR-53g: delayed raw-noise HF preview with canonical ROM checks.
 * No normative smoothing/sine or HE-AAC PCM synthesis occurs. */
#include "sauti_aac_sbr_temporal_mix.h"
#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define SLOTS 32u
#define BANDS 64u
#define COUNT (SLOTS*BANDS)

static void build(unsigned tail,unsigned frame,
                  unsigned *rom_index,
                  SautiAacSbrTemporalNoiseFrame *noise,
                  SautiAacSbrTemporalNoiseReport *n_report,
                  SautiAacSbrComplex signal[COUNT]){
    memset(noise,0,sizeof(*noise));
    memset(n_report,0,sizeof(*n_report));
    memset(signal,0,COUNT*sizeof(*signal));
    const unsigned kx=16u,m=8u;
    unsigned vectors=0u,positive=0u;
    for(unsigned t=0u;t<SLOTS;t++)
        for(unsigned k=0u;k<BANDS;k++){
            const size_t i=(size_t)t*BANDS+k;
            signal[i].re=3.0f;
            signal[i].im=0.25f;
            if(k<kx || k>=kx+m)continue;
            noise->active[i]=1u;
            /* First output is entirely prior; next output inherits
             * exactly tail slots, then switches to current payload. */
            const double amp=(frame==0u || t<tail)?2.0:5.0;
            if(frame==1u && t==30u && k==kx)
                noise->raw_amplitude[i]=0.0;
            else{
                noise->raw_amplitude[i]=amp;
                positive++;
            }
            *rom_index=(*rom_index+1u)&511u;
            assert(sauti_aac_sbr_noise_rom_lookup(
                *rom_index,&noise->vector[i]));
            vectors++;
        }
    n_report->emitted=1u;
    n_report->frame_index=frame;
    n_report->vectors=vectors;
    n_report->nonzero_amplitudes=positive;
    n_report->rom_index=*rom_index;
}
static void assert_atomic_failure(
    const SautiAacSbrComplex signal[COUNT],
    const SautiAacSbrTemporalNoiseFrame *noise,
    const SautiAacSbrTemporalApplyReport *apply,
    const SautiAacSbrTemporalNoiseReport *n_report,
    SautiAacSbrComplex output[COUNT],
    SautiAacSbrTemporalMixReport *stats){
    SautiAacSbrComplex original[COUNT];
    memcpy(original,output,sizeof(original));
    SautiAacSbrTemporalMixReport saved=*stats;
    assert(!sauti_aac_sbr_temporal_noise_mix_preview(
        signal,COUNT,noise,apply,n_report,output,COUNT,stats));
    assert(memcmp(original,output,sizeof(original))==0);
    assert(memcmp(&saved,stats,sizeof(saved))==0);
}
static void run(unsigned tail){
    SautiAacSbrTemporalNoiseFrame noise;
    SautiAacSbrTemporalNoiseReport n_report;
    SautiAacSbrTemporalApplyReport apply={0};
    SautiAacSbrComplex signal[COUNT],output[COUNT];
    SautiAacSbrTemporalMixReport stats={0};
    unsigned index=0u;
    for(unsigned f=0u;f<2u;f++){
        build(tail,f,&index,&noise,&n_report,signal);
        apply.emitted=1u;
        apply.frame_index=f;
        assert(sauti_aac_sbr_temporal_noise_mix_preview(
            signal,COUNT,&noise,&apply,&n_report,output,COUNT,&stats));
        assert(stats.frame_index==f &&
               stats.active_bins==SLOTS*8u &&
               stats.noise_bins==(f?SLOTS*8u-1u:SLOTS*8u) &&
               stats.noise_rms>0.0);
        for(unsigned t=0u;t<SLOTS;t++)
            for(unsigned k=0u;k<BANDS;k++){
                const size_t i=(size_t)t*BANDS+k;
                const double amp=noise.raw_amplitude[i];
                const SautiAacSbrComplex v=noise.vector[i];
                const float exp_re=(float)(3.0+(double)v.re*amp);
                const float exp_im=(float)(0.25+(double)v.im*amp);
                assert(output[i].re==exp_re && output[i].im==exp_im);
            }
        if(f==1u){
            const unsigned kx=16u;
            assert(noise.raw_amplitude[0u*BANDS+kx]==2.0);
            assert(noise.raw_amplitude[(tail-1u)*BANDS+kx]==2.0);
            assert(noise.raw_amplitude[tail*BANDS+kx]==5.0);
            assert(noise.raw_amplitude[30u*BANDS+kx]==0.0);
            /* All error cases must preserve caller output and report. */
            SautiAacSbrTemporalApplyReport wrong_apply=apply;
            wrong_apply.frame_index++;
            assert_atomic_failure(signal,&noise,&wrong_apply,
                                  &n_report,output,&stats);
            SautiAacSbrTemporalNoiseReport bad_report=n_report;
            bad_report.rom_index=(bad_report.rom_index+1u)&511u;
            assert_atomic_failure(signal,&noise,&apply,
                                  &bad_report,output,&stats);
            bad_report=n_report;
            bad_report.nonzero_amplitudes++;
            assert_atomic_failure(signal,&noise,&apply,
                                  &bad_report,output,&stats);
            SautiAacSbrTemporalNoiseFrame bad_noise=noise;
            bad_noise.vector[BANDS*31u+kx].re+=1.0f;
            assert_atomic_failure(signal,&bad_noise,&apply,
                                  &n_report,output,&stats);
            bad_noise=noise;
            bad_noise.raw_amplitude[BANDS*31u+kx]=NAN;
            assert_atomic_failure(signal,&bad_noise,&apply,
                                  &n_report,output,&stats);
            bad_noise=noise;
            bad_noise.active[BANDS*31u+kx]=2u;
            assert_atomic_failure(signal,&bad_noise,&apply,
                                  &n_report,output,&stats);
            bad_noise=noise;
            bad_noise.vector[0u].re=1.0f; /* outside HF mask */
            assert_atomic_failure(signal,&bad_noise,&apply,
                                  &n_report,output,&stats);
            SautiAacSbrComplex bad_signal[COUNT];
            memcpy(bad_signal,signal,sizeof(bad_signal));
            bad_signal[BANDS*31u+kx].re=NAN;
            assert_atomic_failure(bad_signal,&noise,&apply,
                                  &n_report,output,&stats);
            bad_noise=noise;
            bad_noise.raw_amplitude[BANDS*31u+kx]=1e300;
            assert_atomic_failure(signal,&bad_noise,&apply,
                                  &n_report,output,&stats);
        }
    }
    assert(index==((2u*SLOTS*8u)&511u));
    printf("SBR-53g: %u-slot handoff raw ROM preview, phase and atomic rollback PASS\n",tail);
}
static void zero_active(void){
    SautiAacSbrTemporalNoiseFrame noise={0};
    SautiAacSbrTemporalNoiseReport nr={0};
    SautiAacSbrTemporalApplyReport ar={0};
    SautiAacSbrComplex in[COUNT]={0},out[COUNT];
    SautiAacSbrTemporalMixReport stats={0};
    nr.emitted=1u;ar.emitted=1u;
    assert(sauti_aac_sbr_temporal_noise_mix_preview(
        in,COUNT,&noise,&ar,&nr,out,COUNT,&stats));
    assert(stats.active_bins==0u &&
           stats.noise_bins==0u && stats.noise_rms==0.0);
    puts("SBR-53g: empty active mask outputs unchanged complex QMF PASS");
}
int main(void){
    run(2u);
    run(6u);
    zero_active();
    puts("PASS SBR-53g raw complex HF noise preview (no HE-AAC PCM)");
    return 0;
}
