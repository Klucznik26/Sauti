#ifndef SAUTI_AAC_SBR_TEMPORAL_GAIN_BRIDGE_H
#define SAUTI_AAC_SBR_TEMPORAL_GAIN_BRIDGE_H

#include "sauti_aac_sbr_temporal_reference_gain.h"

/* SBR-54m: adapt new SBR-54j/k/l reference pre-limiter/limiter
 * HF gain decisions to the EXISTING tested SBR-53e one-frame
 * delayed full-original-envelope gain-applicator.
 *
 * This keeps the complete FIXVAR handoff logic of SBR-53e
 * unchanged. The returned gain report may only be consumed by
 * a SEPARATE SBR-53e apply state; never by production decoding.
 *
 * This stage transports ONLY G; Q_M and S_M will be evaluated
 * separately in a later joint allocation/PCM stage. The HF-only
 * experiment must not be described as conformant HE-AAC.
 * Atomic: no mutation of *out on any failure.
 */
int sauti_aac_sbr_temporal_gain_bridge(
    const SautiAacSbrTemporalReferenceGainReport *reference,
    SautiAacSbrTemporalGainReport *out);

#endif
