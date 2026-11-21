#ifndef SAUTI_AAC_LTP_H
#define SAUTI_AAC_LTP_H
#include "sauti_aac_syntax.h"
#include "sauti_aac_tns.h"

/* AAC LTP (object type 4, ISO/IEC 14496-3 4.6.6): long-term prediction in the time domain.  The decoder keeps, per channel, its last two frames of output and the windowed second half of
 * the last IMDCT frame (the aliased part not yet complete); a frame with ltp_data takes the signal `lag` samples back, scales it by a gain from the standard's table, windows it like the
 * current frame, transforms it (forward MDCT), runs it through the analysis TNS filter when the frame uses TNS, and adds the result to the dequantised spectrum in the bands flagged in
 * ltp_long_used.  Long windows only; EIGHT_SHORT frames carry no ltp_data.  Time-domain units are those of the decoder (the IMDCT output before the final PCM scaling). */
typedef struct {
    float buf[3072];   /* [0,1024) output two frames ago, [1024,2048) previous output, [2048,3072) previous overlap */
} SautiAacLtpState;

void sauti_aac_ltp_reset(SautiAacLtpState *state);
/* `ics` carries ltp_present / lag / coef / used; `previous_shape` is the window shape of the previous frame (the current one is ics->window_shape).  No-op for frames without ltp_data and
 * for EIGHT_SHORT.  1 = done, 0 = invalid arguments (spectrum unchanged). */
int sauti_aac_ltp_apply(const SautiAacLtpState *state,const SautiAacIcsInfo *ics,unsigned idx,unsigned previous_shape,const SautiAacTnsData *tns,float spectral[1024]);
/* After the frame's synthesis: `pcm` is its output, `overlap` the new overlap (second half of the windowed IMDCT frame). */
void sauti_aac_ltp_update(SautiAacLtpState *state,const float pcm[1024],const float overlap[1024]);
/* The forward MDCT of 2048 windowed samples into 1024 coefficients (decoder units); exposed for tests. */
int sauti_aac_ltp_mdct(const float in[2048],float out[1024]);
#endif
