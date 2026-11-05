#ifndef SAUTI_AAC_SBR_HF_PATCH_ROUTE_AUDIT_H
#define SAUTI_AAC_SBR_HF_PATCH_ROUTE_AUDIT_H

#include "sauti_aac_sbr_temporal_source6.h"

/* SBR-54u: four-band provenance audit of real bitstream HF patch
 * mapping. Side data only; does not modify QMF, gain or PCM.
 *
 * Actual target bands k=18..21 are indexed by k-18. For each output
 * slot, map the ORIGINAL SBR-52 owner to source_for_target[k].
 * Source QMF timing W[i-6] belongs to SBR-54s and is NOT itself
 * the patch source band; never conflate the two.
 *
 * On rejection, the accumulator is unchanged. No heap. */
#define SAUTI_AAC_SBR_ROUTE_BAND_FIRST 18u
#define SAUTI_AAC_SBR_ROUTE_BAND_COUNT 4u

typedef struct {
    uint64_t frames;
    uint64_t mapped[4],outside[4],unmapped[4];
    uint64_t previous_owner[4];
    uint64_t sources[4][32];
} SautiAacSbrHfPatchRouteAuditState;

int sauti_aac_sbr_hf_patch_route_audit_add(
    const SautiAacSbrTemporalHistory *history,
    SautiAacSbrHfPatchRouteAuditState *state);

#endif
