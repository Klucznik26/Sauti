#ifndef SAUTI_AAC_PRED_H
#define SAUTI_AAC_PRED_H
#include "sauti_aac_syntax.h"
#include <stdint.h>

/* AAC Main prediction (ISO/IEC 14496-3 4.6.7 "Prediction"): one backward-adaptive second-order lattice predictor per spectral line (1024 lines of the long window), driven by
 * the decoder's own reconstructed values, so it needs no side information beyond prediction_used / reset flags.  The state variables are kept as 32-bit floats whose low 16 bits are
 * cleared (the standard's 16-bit mantissa storage); the prediction itself is rounded the same way.  One state per channel. */
typedef struct {
    float r0,r1;       /* the two previous reconstructed values (scaled by a) */
    float cor0,cor1;   /* correlation estimates */
    float var0,var1;   /* energy estimates */
} SautiAacPredLine;
typedef struct {
    SautiAacPredLine line[1024];
} SautiAacPredState;

void sauti_aac_pred_reset(SautiAacPredState *state);     /* all lines: r = cor = 0, var = 1 */

/* Applies the predictor to one channel's dequantised spectrum (after M/S, before intensity and TNS, as the spec orders the tools).  Long windows: every line of the bands below
 * PRED_SFB_MAX updates its predictor, and the prediction is added where prediction_used says so; then predictor_reset_group resets every 30th line starting at group - 1.  EIGHT_SHORT:
 * all predictors reset, the spectrum is untouched.  `idx` is the packed index (band tables).  1 = done, 0 = invalid arguments (state and spectrum unchanged). */
int sauti_aac_pred_apply(SautiAacPredState *state,const SautiAacIcsInfo *ics,unsigned idx,float spectral[1024]);
/* The same, and the lines of every PNS (noise) band get no prediction and their predictors start over (the behaviour of FAAD2, which matches to 116 dB on streams with prediction and
 * noise bands; FFmpeg keeps predicting through noise bands and differs).  `noise_band[sfb]` != 0 marks a band of a long window; NULL = none. */
int sauti_aac_pred_apply_pns(SautiAacPredState *state,const SautiAacIcsInfo *ics,unsigned idx,float spectral[1024],const uint8_t *noise_band);
#endif
