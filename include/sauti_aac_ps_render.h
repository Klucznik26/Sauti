#ifndef SAUTI_AAC_PS_RENDER_H
#define SAUTI_AAC_PS_RENDER_H

#include "sauti_aac_ps_hybrid.h"
#include "sauti_aac_ps_matrix.h"
#include "sauti_aac_ps_phase.h"

#include <stdint.h>

/* Prepared, immutable per-frame control data for the later PS hybrid-QMF
 * renderer. This is deliberately not PCM: hybrid analysis, decorrelation,
 * mixing and hybrid synthesis remain separate DSP stages. */
typedef struct {
    SautiAacPsMappedFrame mapped;
    SautiAacPsHybridSchedule schedule;
    SautiAacPsMatrix matrix;
    SautiAacPsComplexMatrix complex_matrix;
} SautiAacPsRenderPlan;

/* Persistent state owned by one PS stream. It only carries the normative
 * IPD/OPD smoothing history; syntax differential state stays with the SBR
 * payload parser. */
typedef struct {
    SautiAacPsPhaseState phase;
    uint64_t frames;
} SautiAacPsRenderState;

/* Compose mapping, hybrid routing, IID/ICC matrix and IPD/OPD phase into one
 * atomic plan. On failure neither state nor plan changes. The resulting plan
 * has no audio samples and cannot substitute for HE-AAC v2 stereo PCM. */
int sauti_aac_ps_render_plan_build(const SautiAacPsFrame *frame,
                                   SautiAacPsRenderState *state,
                                   SautiAacPsRenderPlan *out);

#endif
