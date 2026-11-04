#ifndef SAUTI_AAC_SBR_QMF_FRAME_H
#define SAUTI_AAC_SBR_QMF_FRAME_H

#include "sauti_aac_sbr_qmf_analysis.h"
#include "sauti_aac_sbr_hf_joint_rom.h"
#include "sauti_aac_sbr_hf_predict.h"
#include "sauti_aac_sbr_qmf_synth_mod.h"

/* SBR-28: independent first full-channel QMF framing API:
 * 32 AAC-LC PCM samples per slot -> 32 complex analysis QMF ->
 * HF patch transpose (SBR-12) or caller-supplied LPC/chirp (SBR-13) ->
 * joint HF ROM adjustment (SBR-24) -> 64-band QMF synthesis
 * (SBR-26/25) -> 64 output samples per slot.
 *
 * This consumes DECODED AAC-LC PCM and supplied, decoded SBR geometry,
 * energy, flags, and time grid. It does not decode a HE-AAC bitstream,
 * perform SBR-14 LPC estimation, align normative HF delay or account
 * for possible full-decoder output latency. It is a first integration
 * and is NOT advertised as conformance-quality HE-AAC PCM.
 *
 * Patches may intentionally omit 1-2 highest HF bins. The current
 * SBR-16 limiter cannot synthesize positive envelope energy from
 * zero sources. Such frames are rejected atomically; the caller must
 * not silently discard a positive target energy in an omitted band.
 *
 * The 64-bin composition retains low QMF only in k<kx. k>=kx is
 * assembled from HF and bins k>=k2 stay zero.
 */
typedef struct {
    const SautiAacSbrHfJointConfig *hf;
    const uint8_t *attack;  /* env_count entries, each 0 or 1 */
    unsigned bs_smoothing_mode; /* 0=five-tap; 1=bypass */
    /* BOTH pointers NULL -> direct patch transpose baseline.
     * BOTH non-NULL -> SBR-13 second-order predictor. The LPC
     * coefficients are supplied, NOT re-estimated each frame. */
    const SautiAacSbrLpcCoeffs *lpc;
    const SautiAacSbrChirp *chirp;
} SautiAacSbrQmfFrameConfig;

typedef struct {
    SautiAacSbrQmfAnalysisState analysis;
    SautiAacSbrHfRomState hf;
    SautiAacSbrQmfSynthWindowState synthesis;
    SautiAacSbrComplex low_prior[2u][32u]; /* oldest, newest */
    uint32_t ready; /* zero-init; 1 after first completed frame */
    uint64_t frames; /* successful calls, independent of QMF slots */
} SautiAacSbrQmfFrameState;

/* Process slots=hf->slots in range 1..40.
 * core_pcm: chronological downsampled AAC-LC [slots][32].
 * output: chronological 2x reconstructed signal [slots][64].
 * output_count/core_count count float entries (not bytes).
 * report: caller-owned SBR-16 limiter diagnostics.
 *
 * All outputs and channel state are atomically staged. Input/output
 * may overlap; neither may overlap the state or report objects.
 * On failure output/report/state remain bitwise unchanged.
 * No heap allocation, codec dependencies, or process-global state. */
/* Diagnostic-only failure stage. The original transactional API remains
 * unchanged; _ex reports where a rejected frame stopped, not why.
 * A non-NULL stage pointer is written on all calls. */
typedef enum {
    SAUTI_SBR_QMF_STAGE_INPUT = 0,
    SAUTI_SBR_QMF_STAGE_MAP,
    SAUTI_SBR_QMF_STAGE_HISTORY,
    SAUTI_SBR_QMF_STAGE_ANALYSIS,
    SAUTI_SBR_QMF_STAGE_TRANSPOSE,
    SAUTI_SBR_QMF_STAGE_PREDICT,
    SAUTI_SBR_QMF_STAGE_HF_ADJUST,
    SAUTI_SBR_QMF_STAGE_SYNTHESIS,
    SAUTI_SBR_QMF_STAGE_OK
} SautiAacSbrQmfStage;

int sauti_aac_sbr_qmf_frame_process_ex(
    const SautiAacSbrQmfFrameConfig *config,
    const float *core_pcm,size_t core_count,
    float *output,size_t output_count,
    SautiAacSbrQmfFrameState *state,
    SautiAacSbrLimiterReport *report,
    SautiAacSbrQmfStage *stage);

int sauti_aac_sbr_qmf_frame_process(
    const SautiAacSbrQmfFrameConfig *config,
    const float *core_pcm,
    size_t core_count,
    float *output,
    size_t output_count,
    SautiAacSbrQmfFrameState *state,
    SautiAacSbrLimiterReport *report);

#endif
