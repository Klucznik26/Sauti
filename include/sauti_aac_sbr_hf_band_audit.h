#ifndef SAUTI_AAC_SBR_HF_BAND_AUDIT_H
#define SAUTI_AAC_SBR_HF_BAND_AUDIT_H

#include "sauti_aac_sbr_temporal_source6.h"

/* SBR-54t: read-only per-subband 53b vs source6 complex HF audit.
 * The two inputs are two SAUTI QMF streams, NOT FFmpeg reference.
 * skip_preroll is supplied by 54s; it excludes first-frame
 * unavailable history and keeps the same sampling set as 54s.
 *
 * No PCM and no changes to either QMF history. Atomic on failure.
 */
typedef struct {
    double old_energy[64],new_energy[64],cross[64],difference[64];
    uint64_t active[64],changed[64];
    uint64_t frames,active_total,changed_total;
    double peak_difference;
    uint64_t peak_frame;
    uint8_t peak_slot,peak_band;
} SautiAacSbrHfBandAuditState;

int sauti_aac_sbr_hf_band_audit_add(
    const SautiAacSbrComplex *old_hf,size_t old_count,
    const SautiAacSbrComplex *new_hf,size_t new_count,
    const SautiAacSbrTemporalSource6Report *source6,
    SautiAacSbrHfBandAuditState *state);

#endif
