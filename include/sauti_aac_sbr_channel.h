#ifndef SAUTI_AAC_SBR_CHANNEL_H
#define SAUTI_AAC_SBR_CHANNEL_H

#include "sauti_aac_sbr_hf_predict.h"
#include "sauti_aac_sbr_payload.h"
#include "sauti_aac_sbr_qmf_analysis.h"
#include "sauti_aac_sbr_qmf_synth_mod.h"
#include "sauti_usac_hbe.h"
#include "sauti_aac_sbr_ld_qmf.h"

/* SBR-82: one SBR channel decoded the way the standard lays it out, with all state in one object.
 *
 * Per frame, in the QMF domain (32 slots x 64 bands, slot-major):
 *   - analysis of the 1024 core samples gives 32 new low-band slots; with the 8 slots kept from the previous frame they form
 *     X_low[40][32];
 *   - HF carrier X_high[j] for j = i+2 comes from X_low[j-2..j] (complex second-order prediction with the LPC of this frame and the
 *     chirp factors);
 *   - envelope e covers output slots i in [env_edges[e], env_edges[e+1]); its gain, noise and sine amplitudes come from the
 *     limiter over the carrier power of exactly those slots (so the six slots of look-back are implicit in X_low);
 *   - output slot i is X_low[i+2] in the bands below kx and G*X_high[i+2] + noise + sine above (G smoothed over five slots when
 *     bs_smoothing_mode == 0 except in transient envelopes);
 *   - an envelope that ends after slot 32 spills into the first slots of the next frame: those slots are produced in this call and
 *     handed over, never recomputed.
 * No frame is held back: the grid returned for frame c is complete and in time order, there is nothing to flush at the end.
 *
 * Validation of this module against another decoder happens in tools/tests, not here; no external decoder is used at run time.
 * All state is committed only when the whole frame succeeded. Zero-initialise with sauti_aac_sbr_channel_init. */

#define SAUTI_AAC_SBR_CHANNEL_SLOTS 32u
#define SAUTI_AAC_SBR_CHANNEL_BANDS 64u
#define SAUTI_AAC_SBR_CHANNEL_GRID (SAUTI_AAC_SBR_CHANNEL_SLOTS*SAUTI_AAC_SBR_CHANNEL_BANDS)
#define SAUTI_AAC_SBR_CHANNEL_SPILL 12u                 /* slots an envelope may run past the frame: 6 (2:1) / 12 (4:1) */

typedef struct {
    uint32_t ready;
    uint32_t ld;                                      /* 1: AAC-LD / ELD low-delay SBR (single rate): CLDFB analysis and synthesis; 2: the same at the dual rate (32-band analysis of the core, 64-band synthesis: the output has twice the rate of the core); two slots of history, no look-ahead / spill (frame_slots 16 or 15) */
    SautiAacSbrLdSynthState ld_synth;
    uint32_t frame_slots;                             /* QMF slots per frame of a 32-band 2:1 channel when the frame is not 1024 core samples (960: 30, ELD 480: 30); 0 = 32 (4:1: 64) */
    uint32_t analysis_bands;                          /* 0 / 32: 1024 core samples per frame through the 32-band QMF; 24: eSBR 8:3, 768 core samples through the 24-band QMF; 16: eSBR 4:1, 1024 core samples through the 16-band QMF, 64 slots per frame, grid of 64 * 64 and 4096 output samples (set by the USAC decoder after init) */
    uint64_t frames;
    SautiAacSbrQmfAnalysisState analysis;
    SautiAacSbrQmfSynthWindowState synth;
    SautiAacSbrComplex low_history[14u*32u];          /* last 8 (4:1: 14) low-band slots of the previous frame */
    SautiAacSbrChirp chirp;
    SautiAacSbrPayloadFrame previous;                 /* for the carried-over onset e_a[0] and the sine continuation */
    uint32_t have_previous;
    unsigned previous_last_edge;                      /* env_edges[env_count] of the previous frame (32..38) */
    uint8_t last_sine_before[64];                     /* PVC: ... and the same flags one frame earlier (they decide whether a tone carried over into this frame was already running) */
    uint8_t last_sine[64];                            /* per bin: was a sine added in the last envelope of the previous frame */
    SautiAacSbrComplex spill[SAUTI_AAC_SBR_CHANNEL_SPILL*64u]; /* finished HF of the slots the previous frame ran past its end */
    unsigned spill_slots,spill_kx,spill_k2;
    double gain_history[4][64],noise_history[4][64];  /* gain / noise of the last four processed slots (smoothing) */
    uint32_t history_valid;
    unsigned index_noise,index_sine;
    SautiUsacPvcState pvc;                            /* eSBR PVC: low band energy history of the predictor */
    SautiAacSbrComplex pvc_carry[12*64];              /* PVC: the HF carrier (not yet adjusted) of the time slots the previous frame's grid ran past its end: the adjustment of the next frame starts with them */
    double q_carry[4][64];                            /* PVC: noise floor of the time slots a frame's grid ran past its end (they belong to the first slots of the next frame) */
    SautiUsacHbe hbe;                                 /* eSBR harmonic transposer (bs_harmonicSBR): runs in every frame, its output and the low band lag the QMF analysis by one frame */
    SautiAacSbrComplex hbe_low[2][SAUTI_AAC_SBR_CHANNEL_GRID]; /* low band QMF slots (32 bands; 32 slots, 4:1: 64) of the previous frame [0] and the one before [1] */
    float pv_history[14u*64u][2];                     /* the last 8 (4:1: 14) slots of the transposer output (re, im) */
    uint32_t mps_lf_ahead;                            /* MPEG Surround 212 follows: QMF bands 0..2 of the grid are taken six slots (4:1: twelve) ahead of the others, the hybrid filter bank of the upmix delays them by that much */
    uint64_t discontinuities;                         /* frames whose first border did not continue the previous frame */
} SautiAacSbrChannel;

int sauti_aac_sbr_channel_init(SautiAacSbrChannel *ch);

/* core: 1024 core-rate PCM samples of this frame (768 when analysis_bands == 24); payload: the parsed SBR data of this frame (temporal parse, borders may run past
 * the frame). grid: 32*64 complex, low band below kx, HF in [kx, kx+m), zero above. Returns 1, or 0 with *ch and grid unchanged. */
int sauti_aac_sbr_channel_frame(SautiAacSbrChannel *ch,const float core[1024],
                                const SautiAacSbrPayloadFrame *payload,
                                SautiAacSbrComplex grid[]);
int sauti_aac_sbr_channel_frame_low(SautiAacSbrChannel *ch,const SautiAacSbrComplex *low,const SautiAacSbrPayloadFrame *f,SautiAacSbrComplex grid[]);   /* stereoConfigIndex 3: the low band is given (slots * 32), the QMF analysis state is not touched */

/* 32 slots of the grid -> 2048 PCM samples at twice the core rate (4:1: 64 slots -> 4096 samples; the grid and pcm buffers are twice as large). */
int sauti_aac_sbr_channel_synthesize(SautiAacSbrChannel *ch,
                                     const SautiAacSbrComplex grid[],
                                     float pcm[]);

#endif
