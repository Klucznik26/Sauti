#ifndef SAUTI_AAC_PS_DECORRELATE_H
#define SAUTI_AAC_PS_DECORRELATE_H
#include "sauti_aac_ps_hybrid.h"
#include "sauti_aac_sbr_hf.h"
#include <stddef.h>

/* PS-2b: complex all-pass decorrelation and high-band delays, before
 * transient gain application. Time-major input/output [32][71 or 91].
 * All-pass on bands [0,30) or [0,50), delay-14 until band 42/62,
 * delay-1 on remaining bands. All-pass topology: direct 2-slot delay,
 * three links with 3,4,5 slots, each with phase rotation.
 *
 * Persistent state is one stream, reset when 20/34 topology changes.
 * Input/output/state must be disjoint. On error all outputs and state
 * are unchanged. This stage alone does not output stereo PCM. */
typedef struct {
    SautiAacSbrComplex input_history[91u][14u];
    SautiAacSbrComplex allpass_history[50u][3u][5u];
    unsigned bands; /* 0, 71, or 91 */
} SautiAacPsDecorrelateState;

int sauti_aac_ps_decorrelate(const SautiAacPsHybridSchedule *schedule,
                             const SautiAacSbrComplex *direct,size_t direct_count,
                             SautiAacPsDecorrelateState *state,
                             SautiAacSbrComplex *out,size_t out_count);
#endif
