#ifndef SAUTI_AAC_PS_PCM_STEREO_H
#define SAUTI_AAC_PS_PCM_STEREO_H

#include "sauti_aac_ps_stereo.h"
#include "sauti_aac_ps_hybrid_synth.h"
#include "sauti_aac_sbr_qmf_synth_mod.h"

/* PS-3c: two independent, persistent SBR 64-band QMF synthesis windows.
 * Each frame consumes 32 x 64 complex QMF samples for L and R, and
 * writes 32 x 64 float PCM samples in interleaved LRLR... layout.
 * All input/output/state buffers must be pairwise distinct.
 * No partial output or state is committed on failure. */
typedef struct {
    SautiAacSbrQmfSynthWindowState left;
    SautiAacSbrQmfSynthWindowState right;
} SautiAacPsPcmStereoState;

int sauti_aac_ps_pcm_stereo_from_qmf(
    const SautiAacSbrComplex *left_qmf,size_t left_count,
    const SautiAacSbrComplex *right_qmf,size_t right_count,
    SautiAacPsPcmStereoState *state,
    float *pcm_interleaved,size_t pcm_count);

/* Full PS rendering from 38-slot mono SBR QMF and prepared PS
 * render-plan schedule/complex matrix, through:
 * hybrid analysis -> decorrelation -> stereo mix -> hybrid synthesis
 * -> two SBR QMF synthesis windows -> interleaved stereo waveform.
 * This API does NOT decode ADTS/SBR payloads and is not a validated
 * HE-AAC v2 bitstream decoder. Caller supplies decoded, aligned QMF. */
typedef struct {
    SautiAacPsStereoState hybrid;
    SautiAacPsPcmStereoState synthesis;
} SautiAacPsPcmPipelineState;

int sauti_aac_ps_pcm_stereo_from_mono_qmf(
    const SautiAacPsHybridSchedule *schedule,
    const SautiAacPsComplexMatrix *matrix,
    const SautiAacSbrComplex *mono_qmf,size_t mono_count,
    SautiAacPsPcmPipelineState *state,
    float *pcm_interleaved,size_t pcm_count);

#endif
