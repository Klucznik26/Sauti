#ifndef SAUTI_AAC_PS_QMF_BANK_H
#define SAUTI_AAC_PS_QMF_BANK_H

#include "sauti_aac_ps_qmf_low.h"
#include "sauti_aac_ps_qmf_high.h"

/* PS-1c: one frame of complex hybrid QMF for 20/34 parameter modes.
 * Input: time-major [38][64] QMF, first 32 slots at their actual time.
 * Output: time-major [32][71 or 91], low bins followed by unsplit high.
 * State and output are both committed atomically on success.
 * No input/output/state aliasing; no PS decorrelation or stereo synthesis.
 * Source data from SBR must include six subsequent QMF slots for FIR
 * lookahead (or be explicitly zero-padded at true end of stream). */
int sauti_aac_ps_qmf_bank_analyze(const SautiAacPsHybridSchedule *schedule,
                                  const SautiAacSbrComplex *qmf,size_t qmf_count,
                                  SautiAacPsQmfLowState *state,
                                  SautiAacSbrComplex *out,size_t out_count);
#endif
